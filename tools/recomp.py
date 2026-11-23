#!/usr/bin/env python3
"""Static recompiler: libtar-android.so (ARMv5TE Thumb-1 + Thumb-2 patch segment) -> C.

Every function of the original .so becomes `void f_<vaddr>(Cpu *c)` operating on the ARM
register file.  Addresses in names are ELF vaddrs; Ghidra addresses are vaddr + 0x10000.

Inputs : work/apk/lib/armeabi/libtar-android.so, work/flow.txt (ghidra_scripts/ExportFlow.java),
         syms_dem.txt (llvm-cxxfilt output of the dynamic symbols)
Outputs: src/gen/*.c, src/gen/gen.h, src/gen/imports.inc, src/gen/image.inc
"""
import os, sys, struct, bisect, collections
from capstone import *
from capstone.arm import *
from elftools.elf.elffile import ELFFile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SO = os.path.join(ROOT, 'work/apk/lib/armeabi/libtar-android.so')
OUT = os.path.join(ROOT, 'src/gen')
GH = 0x10000  # ghidra image base offset

data = open(SO, 'rb').read()
elf = ELFFile(open(SO, 'rb'))
SEGS = [(s['p_vaddr'], s['p_offset'], s['p_filesz'], s['p_memsz'], s['p_flags'])
        for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
TEXT = (0xa3e40, 0xa3e40 + 786828)
PATCH = (0x19c000, 0x19e000)
PLT = (0xa39a8, 0xa39a8 + 1172)


def is_code(a):
    return TEXT[0] <= a < TEXT[1] or PATCH[0] <= a < PATCH[1]


def foff(va):
    for v, o, fs, ms, fl in SEGS:
        if v <= va < v + fs:
            return va - v + o
    raise KeyError(hex(va))


def rd32(va):
    return struct.unpack('<I', data[foff(va):foff(va) + 4])[0]


# ---------------------------------------------------------------- symbols / imports
dynsym = elf.get_section_by_name('.dynsym')
names = {}  # vaddr(even) -> demangled name
for l in open(os.path.join(ROOT, 'syms_dem.txt'), encoding='utf-8', errors='replace'):
    p = l.rstrip('\n').split(' ', 2)
    if len(p) < 3:
        continue
    a = int(p[0], 16)
    if is_code(a & ~1) and int(p[1]) > 0:
        names.setdefault(a & ~1, p[2])

relplt = list(elf.get_section_by_name('.rel.plt').iter_relocations())
plt_imports = {}  # plt entry vaddr -> import name
for i, r in enumerate(relplt):
    plt_imports[PLT[0] + 20 + 12 * i] = dynsym.get_symbol(r['r_info_sym']).name
undef_names = sorted({s.name for s in dynsym.iter_symbols() if s['st_shndx'] == 'SHN_UNDEF' and s.name})

# ---------------------------------------------------------------- ghidra flow info
gfuncs = {}       # entry -> name
gbody = {}        # entry -> list of (lo, hi) inclusive
switch = {}       # jump insn addr -> [targets]
cur = None
for l in open(os.path.join(ROOT, 'work/flow.txt')):
    p = l.split()
    if p[0] == 'F':
        a = int(p[1], 16) - GH
        if is_code(a) and 'vtable' not in p[2]:
            cur = a
            gfuncs[a] = p[2]
            gbody[a] = []
        else:
            cur = None
    elif p[0] == 'R' and cur is not None:
        gbody[cur].append((int(p[1], 16) - GH, int(p[2], 16) - GH))
    elif p[0] == 'J':
        switch[int(p[1], 16) - GH] = [int(x, 16) - GH for x in p[2:]]

# map every address to the ghidra function body containing it
body_owner = []
for e, rs in gbody.items():
    for lo, hi in rs:
        body_owner.append((lo, hi, e))
body_owner.sort()
bo_keys = [b[0] for b in body_owner]


def owner(a):
    i = bisect.bisect_right(bo_keys, a) - 1
    if i >= 0 and body_owner[i][0] <= a <= body_owner[i][1]:
        return body_owner[i][2]
    return None


# ---------------------------------------------------------------- decoding
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
md.detail = True
_dcache = {}


def dec(a):
    if a in _dcache:
        return _dcache[a]
    o = foff(a)
    i = next(md.disasm(data[o:o + 4], a, 1), None)
    _dcache[a] = i
    return i


def rn(i, r):
    n = i.reg_name(r)
    return {'sb': 'r9', 'sl': 'r10', 'fp': 'r11', 'ip': 'r12'}.get(n, n)


def bl_target(i):
    return i.operands[0].imm


ghidra_insns = set()
for _l in open(os.path.join(ROOT, 'work/flow.txt')):
    if _l.startswith('I '):
        ghidra_insns.add(int(_l.split()[1], 16) - GH)

# ---------------------------------------------------------------- switch tables Ghidra missed
def recover_switch(a):
    """gcc thumb1 PIC switch:  cmp rI,#N; b(ls|hi) ..; ldr rB,[pc,#k]; lsls rI,rI,#2; add rB,pc;
    ldr rE,[rI,rB]; adds rT,rE,rB; mov pc,rT   -> targets = table + entry, N+1 entries"""
    seq = []
    p = a
    for _ in range(14):
        p -= 2
        i = dec(p)
        if i is not None:
            seq.append(i)
    base = None
    bound = None
    ireg = None   # index register (lsls rI, rI, #2): only a cmp of it is the bound check
    tbl_ld = None  # address of the ldr rB,[pc,#k] that loads the table offset
    for i in seq:  # walking backwards
        m = i.mnemonic
        if m == 'lsls' and ireg is None and base is not None and len(i.operands) == 3                 and i.operands[2].type == ARM_OP_IMM and i.operands[2].imm == 2 and i.operands[0].reg == i.operands[1].reg:
            ireg = rn(i, i.operands[0].reg)
        if m == 'add' and len(i.operands) == 2 and rn(i, i.operands[1].reg) == 'pc' and base is None:
            breg = rn(i, i.operands[0].reg)
            # find the literal load of breg before it
            q = i.address - 2
            for _ in range(6):
                j = dec(q)
                if j and j.mnemonic == 'ldr' and rn(j, j.operands[0].reg) == breg and j.operands[1].type == ARM_OP_MEM and rn(j, j.operands[1].mem.base) == 'pc':
                    lit = ((j.address + 4) & ~3) + j.operands[1].mem.disp
                    base = (rd32(lit) + i.address + 4) & 0xffffffff
                    tbl_ld = j.address
                    break
                q -= 2
        if m == 'movs' and ireg is not None and rn(i, i.operands[0].reg) == ireg:
            break  # index set right here (one of several paths into the table): the bound check is elsewhere
        if m == 'cmp' and base is not None and i.operands[1].type == ARM_OP_IMM                 and (ireg is None or rn(i, i.operands[0].reg) == ireg):
            bound = i.operands[1].imm
            break
    if base is None:
        return None
    if bound is None and tbl_ld is not None:
        # the range check branches into the table load from elsewhere: cmp rX,#N; bls <table load>
        # (cIdleKeyProcess::ScriptKeyProcess: 000c3bfa cmp r3,#0xe; bls 000c3bd2, other paths set r3 with movs and
        # jump to 000c3bd2 too)
        for q in range(tbl_ld - 0x400, tbl_ld + 0x400, 2):
            j = dec(q) if is_code(q) else None
            if j is None or j.mnemonic != 'cmp' or len(j.operands) != 2 or j.operands[1].type != ARM_OP_IMM:
                continue
            k = dec(q + 2) if is_code(q + 2) else None
            if k is not None and k.mnemonic == 'bls' and k.operands[0].type == ARM_OP_IMM and k.operands[0].imm == tbl_ld:
                bound = j.operands[1].imm
                _sw_remote[a] = True
                break
    if bound is None or bound > 1024:
        # bound check is in another block: take entries while they land on instructions near the jump
        out = []
        for k in range(512):
            try:
                e = rd32(base + 4 * k)
            except KeyError:
                break
            t = (base + e) & 0xffffffff
            if t & 1 or not is_code(t) or abs(t - a) > 0x1000 or dec(t) is None:
                break
            out.append(t)
        return out or None
    out = []
    for k in range(bound + 1):
        try:
            e = rd32(base + 4 * k)
        except KeyError:
            return None
        t = (base + e) & 0xffffffff
        if not is_code(t & ~1):
            return None
        out.append(t & ~1)
    return out


_sw_cache = {}
_sw_remote = {}  # jump addr -> True when the bound came from a cmp/bls elsewhere in the function
unresolved_jumps = set()


def switch_targets(a):
    if a not in _sw_cache:
        _sw_cache[a] = recover_switch(a) or []
    if switch.get(a):
        # Ghidra's table plus what the remote bound check covers (Ghidra lists 3 of the 6 targets of the
        # 15-entry table at cIdleKeyProcess::ScriptKeyProcess @000c3bdc; the bungee jump goes to 000c3c16)
        extra = [t for t in _sw_cache[a] if t not in switch[a]] if _sw_remote.get(a) else []
        if extra:
            print(f'switch {a:06x}: target(s) {[hex(t) for t in extra]} added to the Ghidra table {[hex(t) for t in switch[a]]}')
            switch[a] = switch[a] + extra
        return switch[a]
    return _sw_cache[a]

# ---------------------------------------------------------------- function entries
entries = set(names) | set(gfuncs)
for v in struct.unpack('<2I', data[foff(0x186000):foff(0x186000) + 8]):
    entries.add(v & ~1)
# code pointers in data via R_ARM_RELATIVE
for r in elf.get_section_by_name('.rel.dyn').iter_relocations():
    if r['r_info_type'] == 23:
        v = rd32(r['r_offset'])
        if v & 1 and is_code(v & ~1):
            entries.add(v & ~1)
    elif r['r_info_type'] == 2 and r['r_info_sym']:
        s = dynsym.get_symbol(r['r_info_sym'])
        if s['st_shndx'] != 'SHN_UNDEF' and s['st_value'] & 1:
            entries.add(s['st_value'] & ~1)
# code pointers built in code: ldr rD, [pc, #k] ... add rD, pc  (value has the thumb bit)
for _a in sorted(ghidra_insns):
    if not is_code(_a):
        continue
    _i = dec(_a)
    if _i is None or _i.mnemonic != 'add' or len(_i.operands) != 2 or _i.operands[1].type != ARM_OP_REG or rn(_i, _i.operands[1].reg) != 'pc':
        continue
    _d = rn(_i, _i.operands[0].reg)
    _q = _a - 2
    for _ in range(8):
        _j = dec(_q) if is_code(_q) else None
        if _j and _j.mnemonic == 'ldr' and _j.operands[0].type == ARM_OP_REG and rn(_j, _j.operands[0].reg) == _d                 and _j.operands[1].type == ARM_OP_MEM and rn(_j, _j.operands[1].mem.base) == 'pc':
            _v = (rd32(((_j.address + 4) & ~3) + _j.operands[1].mem.disp) + _a + 4) & 0xffffffff
            if _v & 1 and is_code(_v & ~1):
                entries.add(_v & ~1)
            break
        _q -= 2
entries = {e for e in entries if is_code(e) and dec(e) is not None}


def is_local_bl(site, tgt):
    """gcc thumb1 'far jump' uses BL inside the same function"""
    o = owner(site)
    return o is not None and o == owner(tgt) and tgt not in entries


# global pass: bl targets that are not entries become entries (unless local far jump)
changed = True
seen_all = set()
while changed:
    changed = False
    work = list(entries - seen_all)
    while work:
        e = work.pop()
        if e in seen_all:
            continue
        seen_all.add(e)
        stack = [e]
        vis = set()
        while stack:
            a = stack.pop()
            while a not in vis and is_code(a):
                vis.add(a)
                i = dec(a)
                if i is None:
                    break
                m = i.mnemonic.split('.')[0]
                if m == 'bl':
                    t = bl_target(i)
                    if is_code(t) and t not in entries and not is_local_bl(a, t):
                        entries.add(t); work.append(t); changed = True
                    elif is_code(t) and is_local_bl(a, t):
                        stack.append(t)
                cc = i.cc
                if i.group(ARM_GRP_JUMP) and m not in ('bl', 'blx', 'bx') and i.operands and i.operands[0].type == ARM_OP_IMM:
                    t = i.operands[0].imm
                    if t not in entries:
                        stack.append(t)
                    if cc == ARM_CC_AL or cc == ARM_CC_INVALID:
                        break
                if m in ('bx',) or (m == 'pop' and any(rn(i, o.reg) == 'pc' for o in i.operands)) or \
                        (i.operands and i.operands[0].type == ARM_OP_REG and rn(i, i.operands[0].reg) == 'pc' and m in ('mov', 'add', 'ldr')):
                    stack.extend(switch_targets(a))
                    break
                if m in ('svc', 'bkpt', 'udf'):
                    break
                a += i.size
                if a in entries:
                    break

# ---------------------------------------------------------------- C emission
COND = {
    ARM_CC_EQ: 'z', ARM_CC_NE: '!z', ARM_CC_HS: 'cf', ARM_CC_LO: '!cf', ARM_CC_MI: 'n', ARM_CC_PL: '!n',
    ARM_CC_VS: 'vf', ARM_CC_VC: '!vf', ARM_CC_HI: '(cf && !z)', ARM_CC_LS: '(!cf || z)',
    ARM_CC_GE: '(n == vf)', ARM_CC_LT: '(n != vf)', ARM_CC_GT: '(!z && n == vf)', ARM_CC_LE: '(z || n != vf)',
}
REGS = ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10', 'r11', 'r12', 'sp', 'lr']
SYNC_ALL_OUT = ' '.join(f'c->r[{k}]={r};' for k, r in enumerate(REGS))
# full sync: patched code in this .so relies on the caller's callee-saved registers (e.g. cAndDevice::DetectDeviceSpec)
SYNC_ARGS_OUT = ' '.join(f'c->r[{k}]={r};' for k, r in enumerate(REGS[:14]))
SYNC_ARGS_IN = ' '.join(f'{r}=c->r[{k}];' for k, r in enumerate(REGS[:14]))
SYNC_ALL_IN = ' '.join(f'{r}=c->r[{k}];' for k, r in enumerate(REGS[:14]))

unsupported = collections.Counter()
import_used = set()


# functions wrapped by host code (src/hooks.c): generated as f_XXXXXX_orig, f_XXXXXX is the host wrapper
HOOKS = {0x0b421c,  # cCreature::SubHp (test-mode damage floor, EH_TESTBUFF)
         0x1100fc,  # cHitAffair::UpdateProcess (test-mode: player attacks always hit, EH_TESTBUFF)
         0x0b3c04,  # cCharacter::ExternalMsgProcessImpl (test-mode: no instant-kill messages, EH_TESTGOD)
         0x11d154, 0x11d44c,  # cGameWorld::CheckMoveObj / CheckMoveTile (EH_MOVETRACE)
         0x0c37c8, 0x0c34cc,  # cIdleKeyProcess::SlotKeyProcess / MoveKeyProcess (EH_MOVETRACE)
         0x11cb20,  # cGameWorld::GetEventObjWhenTouch (EH_MOVETRACE)
         0x0e8834,  # cBattleUi::HitTestImpl (EH_MOVETRACE)
         0x0bce00, 0x0c5b48,  # cActiveObj::MoveImpl / cMoveKeyProcess::Initialize (EH_MOVETRACE)
         0x117884,  # cGameManager::PendMoveField (EH_MOVETRACE)
         0x118de0, 0x11ff0c,  # cGameWorld::IsDoneEvent / cInventory::GetDuplicateItemCount (EH_CONDTRACE)
         0x15c82c, 0x13f364,  # cflScriptBase::ScriptCall / cMonsterBossDragon::ProcUserAct (EH_CONDTRACE)
         0x15c9d0,  # cflScriptBase::NextCommand (EH_SCPTRACE)
         0x0d1a50,  # cflScript::RunRunningGame_Final (script cmd 0xE4 trade-center connection: offline, skipped)
         0x135214, 0x1352ac, 0x1352e4, 0x13285c, 0x1350a4,  # save path tracing (EH_SAVETRACE)
         0x11be54, 0x10027c, 0x0c79a4, 0x19d900,  # developer option gold / gathering / refine multipliers (hooks.c)
         0x134904,  # cGameManager::InitSystemOpt (port default volumes BGM 40 / FX 50, hooks.c)
         0x157720,  # cGlFbGraphics::Setup (framebuffer width follows the display aspect, ui/)
         0x112cd8, 0x113e90,  # cField::DrawBg / DrawSkyBg (backgrounds repeat across views wider than 560, hooks.c)
         0x0edacc,  # cFormWorldMap::OnBgDraw (second copy tested against the whole screen, hooks.c)
         0x12273c,  # cGameTipMgr::DrawLoadingTip (EH_SENTINEL coverage of the loading screen, hooks.c)
         0x0a97cc, 0x0a99ec, 0x0d0af4, 0x11bb48,  # Eamoru sword summon / idle, WalkAnObject, CreateTimeBird (512 logic box, hooks.c)
         0x0c1384, 0x0c1510,  # cRainEffect::CreateRain / Draw (512-px rain repeated across wider views, hooks.c)
         0x11a45c, 0x142d40, 0x1437d0, 0x1437f0, 0x157368, 0x0f4014, 0x11e574,  # world-frame capture (ui/capture.cpp)
         0x141b84, 0x141c14, 0x1422bc, 0x141ccc, 0x144608, 0x15b8dc, 0x14958c}  # interface draw recording (ui/autoform.cpp)
# the interpreter build (src/interp.c, the web build) takes the same list from src/hooks.h: they must agree
import re as _re
_HDR = {int(v, 16) for v in _re.findall(r'X\(([0-9a-f]{6})\)', open(os.path.join(ROOT, 'src', 'hooks.h')).read())}
if _HDR != HOOKS:
    sys.exit('src/hooks.h and HOOKS differ: only in hooks.h %s, only here %s' % (sorted(map(hex, _HDR - HOOKS)), sorted(map(hex, HOOKS - _HDR))))


def fname(a):
    return 'f_%06x' % a


class Fn:
    def __init__(self, e):
        self.e = e
        self.lines = []
        self.labels = set()
        self.calls = set()


def emit_function(e):
    # recursive descent collecting instructions for this function
    insns = {}
    stack = [e]
    labels = {e}
    while stack:
        a = stack.pop()
        while a not in insns and is_code(a):
            i = dec(a)
            if i is None:
                insns[a] = None
                break
            insns[a] = i
            m = i.mnemonic.split('.')[0]
            if m == 'bl':
                t = bl_target(i)
                if is_code(t) and t not in entries:
                    labels.add(t); stack.append(t)
                    if not is_local_bl(a, t):
                        pass
            if i.group(ARM_GRP_JUMP) and m not in ('bl', 'blx', 'bx') and i.operands and i.operands[0].type == ARM_OP_IMM:
                t = i.operands[0].imm
                if t not in entries or t == e:
                    labels.add(t); stack.append(t)
                if i.cc in (ARM_CC_AL, ARM_CC_INVALID):
                    break
            writes_pc = m == 'bx' or (m == 'pop' and any(rn(i, o.reg) == 'pc' for o in i.operands)) or \
                (i.operands and i.operands[0].type == ARM_OP_REG and rn(i, i.operands[0].reg) == 'pc' and m in ('mov', 'add', 'ldr'))
            if writes_pc:
                for t in switch_targets(a):
                    labels.add(t); stack.append(t)
                break
            if m in ('svc', 'bkpt', 'udf'):
                break
            a += i.size
            if a in entries and a != e:
                labels.add(a)  # becomes a fall-through tail call
                insns.setdefault(a, 'TAIL')
                break

    out = []
    gname = names.get(e) or gfuncs.get(e, '?')
    out.append(f'/* {gname} @ ghidra {e + GH:08x} (vaddr {e:06x}) */')
    out.append(f'void {fname(e)}{"_orig" if e in HOOKS else ""}(Cpu *c) {{')
    out.append('  uint32_t ' + ', '.join(f'{r}=c->r[{k}]' for k, r in enumerate(REGS)) + ';')
    out.append('  const uint32_t ret_lr = lr; uint32_t n = 0, z = 0, cf = 0, vf = 0, t0, t1; uint64_t w;')
    out.append('  (void)n; (void)z; (void)cf; (void)vf; (void)t0; (void)t1; (void)w; (void)ret_lr;')
    order = sorted(insns)
    if order[0] != e:
        out.append(f'  goto L_{e:x};')
    prev_falls = None
    for idx, a in enumerate(order):
        i = insns[a]
        if prev_falls is not None and prev_falls != a:
            out.append(f'  goto L_{prev_falls:x};')
        prev_falls = None
        out.append(f' L_{a:x}:')
        if i == 'TAIL':
            out.append(f'  {SYNC_ALL_OUT} {fname(a)}(c); return; /* fallthrough into next function */')
            continue
        if i is None:
            out.append(f'  rc_trap(c, 0x{a:x}, "undecodable"); return;')
            continue
        body, falls = translate(i, e, labels)
        out.append(f'  /* {a:06x}: {i.mnemonic} {i.op_str} */')
        out.extend('  ' + b for b in body)
        if falls:
            prev_falls = a + i.size
    if prev_falls is not None:
        out.append(f'  goto L_{prev_falls:x};')
    out.append('}')
    # drop unused labels (MSVC warns otherwise; harmless)
    return out


def reg(i, op):
    return rn(i, op.reg)


def val(i, op, pc):
    if op.type == ARM_OP_REG:
        r = reg(i, op)
        if r == 'pc':
            return f'(IMG + 0x{pc:x}u)'
        s = r
        if op.shift.type != ARM_SFT_INVALID and op.shift.value:
            st, sv = op.shift.type, op.shift.value
            if st == ARM_SFT_LSL: s = f'({r} << {sv})'
            elif st == ARM_SFT_LSR: s = f'({r} >> {sv})'
            elif st == ARM_SFT_ASR: s = f'(uint32_t)((int32_t){r} >> {sv})'
            elif st == ARM_SFT_ROR: s = f'ror32({r}, {sv})'
            else: raise ValueError('shift')
        return s
    if op.type == ARM_OP_IMM:
        return f'0x{op.imm & 0xffffffff:x}u'
    raise ValueError('operand')


def nz(r):
    return f'n = {r} >> 31; z = {r} == 0;'


def unwind_check(ret):
    """a callee returned to an address other than its own return address (gcc far-jump epilogues):
    keep unwinding until the frame whose call site matches"""
    return f'if (c->unwind) {{ if (c->unwind != IMG + 0x{ret | 1:x}u) return; c->unwind = 0; }}'


def call_direct(t, a, i, full=False):
    ret = a + i.size
    if t in plt_imports:
        nm = plt_imports[t]
        import_used.add(nm)
        return [f'{SYNC_ARGS_OUT} c->r[14] = IMG + 0x{ret | 1:x}u; imp_{nm}(c); {unwind_check(ret)} {SYNC_ARGS_IN} lr = IMG + 0x{ret | 1:x}u;']
    full = full or PATCH[0] <= t < PATCH[1] or PATCH[0] <= a < PATCH[1]
    if full:
        return [f'{SYNC_ALL_OUT} c->r[14] = IMG + 0x{ret | 1:x}u; {fname(t)}(c); {unwind_check(ret)} {SYNC_ALL_IN} lr = IMG + 0x{ret | 1:x}u;']
    return [f'{SYNC_ARGS_OUT} c->r[14] = IMG + 0x{ret | 1:x}u; {fname(t)}(c); {unwind_check(ret)} {SYNC_ARGS_IN} lr = IMG + 0x{ret | 1:x}u;']


def tail_direct(t):
    if t in plt_imports:
        nm = plt_imports[t]
        import_used.add(nm)
        return [f'{SYNC_ALL_OUT} imp_{nm}(c); return;']
    return [f'{SYNC_ALL_OUT} {fname(t)}(c); return;']


def jump_reg(x, a, e):
    """indirect transfer to value x: return if it is our return address, else dispatch"""
    lines = []
    i0 = dec(a)
    if i0 is not None and i0.mnemonic in ('mov', 'add') and not switch_targets(a):
        unresolved_jumps.add(a)
    if switch_targets(a):
        lines.append(f'switch (({x}) - IMG) {{')
        for t in sorted(set(switch_targets(a))):
            lines.append(f'  case 0x{t:x}u: case 0x{t | 1:x}u: goto L_{t:x};')
        lines.append('  default: break; }')
    lines.append(f'if (({x}) == ret_lr) {{ {SYNC_ALL_OUT} return; }}')
    lines.append(f't0 = {x}; {SYNC_ALL_OUT} rc_jump(c, t0, 0x{a:x}u); return;')
    return lines


def mem_addr(i, op, pc):
    m = op.mem
    base = rn(i, m.base)
    if base == 'pc':
        b = (pc & ~3)
        if m.index:
            raise ValueError('pc index')
        return None, b + m.disp
    s = base
    if m.index:
        idx = rn(i, m.index)
        if op.shift.type == ARM_SFT_LSL and op.shift.value:
            idx = f'({idx} << {op.shift.value})'
        s = f'({base} {"-" if op.subtracted else "+"} {idx})'
    elif m.disp:
        s = f'({base} + 0x{m.disp & 0xffffffff:x}u)' if m.disp > 0 else f'({base} - 0x{-m.disp:x}u)'
    return s, None


LDT = {'ldr': 'M32', 'ldrb': 'M8', 'ldrh': 'M16', 'ldrsb': 'MS8', 'ldrsh': 'MS16'}
STT = {'str': 'M32', 'strb': 'M8', 'strh': 'M16'}


def translate(i, e, labels):
    a = i.address
    pc = a + 4
    m = i.mnemonic
    base_m = m.split('.')[0]
    ops = i.operands
    S = i.update_flags
    cc = i.cc
    body = []

    def R(k):
        return reg(i, ops[k])

    # ------------------------------------------------ branches
    if base_m == 'bl':
        t = bl_target(i)
        if is_code(t) and t not in entries:
            return [f'lr = IMG + 0x{(a + 4) | 1:x}u; goto L_{t:x}; /* far jump */'], False
        if not is_code(t) and t not in plt_imports:
            return [f'rc_trap(c, 0x{a:x}, "bl outside code");'], False
        return call_direct(t, a, i), True
    if base_m == 'blx':
        if ops[0].type == ARM_OP_IMM:
            t = ops[0].imm
            if t in plt_imports:
                return call_direct(t, a, i), True
            if t == 0x163fbc:
                import_used.add('__aeabi_atexit')
                return [f'{SYNC_ARGS_OUT} c->r[14] = IMG + 0x{(a + 4) | 1:x}u; imp___aeabi_atexit(c); {unwind_check(a + 4)} {SYNC_ARGS_IN}'], True
            return [f'rc_trap(c, 0x{a:x}, "blx to arm");'], False
        x = R(0)
        ret = (a + i.size) | 1
        return [f't0 = {x}; {SYNC_ARGS_OUT} c->r[14] = lr = IMG + 0x{ret:x}u; rc_dispatch(c, t0); {unwind_check(ret)} {SYNC_ARGS_IN} lr = IMG + 0x{ret:x}u;'], True
    if base_m == 'bx':
        return jump_reg(R(0), a, e), False
    if i.group(ARM_GRP_JUMP) and base_m.startswith('b') and ops and ops[0].type == ARM_OP_IMM:
        t = ops[0].imm
        if t in entries and t != e:
            tgt = tail_direct(t)
        elif not is_code(t):
            tgt = [f'rc_trap(c, 0x{a:x}, "branch outside code"); return;']
        else:
            tgt = [f'goto L_{t:x};']
        if cc in (ARM_CC_AL, ARM_CC_INVALID):
            return tgt, False
        return [f'if ({COND[cc]}) {{ ' + ' '.join(tgt) + ' }'], True
    if cc not in (ARM_CC_AL, ARM_CC_INVALID):
        raise ValueError('conditional non-branch ' + m)

    # ------------------------------------------------ push / pop / ldm / stm
    if base_m == 'push':
        rl = [reg(i, o) for o in ops]
        body.append(f'sp -= {4 * len(rl)};')
        for k, r in enumerate(rl):
            body.append(f'M32(sp + {4 * k}) = {r};')
        return body, True
    if base_m == 'pop':
        rl = [reg(i, o) for o in ops]
        for k, r in enumerate(rl):
            if r == 'pc':
                body.append(f't1 = M32(sp + {4 * k});')
            else:
                body.append(f'{r} = M32(sp + {4 * k});')
        body.append(f'sp += {4 * len(rl)};')
        if 'pc' in rl:
            body += jump_reg('t1', a, e)
            return body, False
        return body, True
    if base_m in ('ldm', 'ldmia', 'stm', 'stmia'):
        b = R(0)
        rl = [reg(i, o) for o in ops[1:]]
        body.append(f't0 = {b};')
        for k, r in enumerate(rl):
            if base_m.startswith('ldm'):
                body.append(f'{r} = M32(t0 + {4 * k});')
            else:
                body.append(f'M32(t0 + {4 * k}) = {r};')
        if i.writeback:
            body.append(f'{b} = t0 + {4 * len(rl)};')
        if 'pc' in rl:
            raise ValueError('ldm pc')
        return body, True

    # ------------------------------------------------ loads / stores
    if base_m in LDT or base_m in STT:
        if len(ops) == 3 and ops[2].type in (ARM_OP_IMM, ARM_OP_REG) and ops[1].type == ARM_OP_MEM:
            # post-indexed  ldr r0, [r1], #4
            s, _ = mem_addr(i, ops[1], pc)
            off = val(i, ops[2], pc)
            sign = '-' if ops[2].subtracted else '+'
            bn = rn(i, ops[1].mem.base)
            if base_m in LDT:
                body.append(f't0 = {bn}; {R(0)} = {LDT[base_m]}(t0); {bn} = t0 {sign} {off};')
            else:
                body.append(f't0 = {bn}; {STT[base_m]}(t0) = {R(0)}; {bn} = t0 {sign} {off};')
            return body, True
        s, lit = mem_addr(i, ops[1], pc)
        if lit is not None:
            if base_m != 'ldr':
                raise ValueError('literal non-word')
            v = rd32(lit)
            if R(0) == 'pc':
                raise ValueError('ldr pc literal')
            return [f'{R(0)} = 0x{v:x}u; /* literal @{lit:x} */'], True
        if R(0) == 'pc':
            body.append(f't1 = M32({s});')
            body += jump_reg('t1', a, e)
            return body, False
        if base_m in LDT:
            if i.writeback:  # pre-indexed with writeback
                bn = rn(i, ops[1].mem.base)
                body.append(f'{bn} = {s}; {R(0)} = {LDT[base_m]}({bn});')
            else:
                body.append(f'{R(0)} = {LDT[base_m]}({s});')
        else:
            if i.writeback:
                bn = rn(i, ops[1].mem.base)
                body.append(f'{bn} = {s}; {STT[base_m]}({bn}) = {R(0)};')
            else:
                body.append(f'{STT[base_m]}({s}) = {R(0)};')
        return body, True

    # ------------------------------------------------ pc-relative address
    if base_m == 'adr':
        return [f'{R(0)} = IMG + 0x{(pc & ~3) + ops[1].imm:x}u;'], True
    if base_m in ('add', 'addw') and len(ops) == 3 and ops[1].type == ARM_OP_REG and R(1) == 'pc' and ops[2].type == ARM_OP_IMM:
        return [f'{R(0)} = IMG + 0x{(pc & ~3) + ops[2].imm:x}u;'], True

    # ------------------------------------------------ data processing
    d = R(0) if ops and ops[0].type == ARM_OP_REG else None
    if base_m in ('cmp', 'cmn', 'tst'):
        x, y = val(i, ops[0], pc), val(i, ops[1], pc)
        if base_m == 'cmp':
            body.append(f't0 = {x}; t1 = {y}; w = (uint64_t)t0 - t1; cf = t0 >= t1; vf = ((t0 ^ t1) & (t0 ^ (uint32_t)w)) >> 31; t0 = (uint32_t)w; {nz("t0")}')
        elif base_m == 'cmn':
            body.append(f't0 = {x}; t1 = {y}; w = (uint64_t)t0 + t1; cf = (uint32_t)(w >> 32); vf = ((t0 ^ (uint32_t)w) & (t1 ^ (uint32_t)w)) >> 31; t0 = (uint32_t)w; {nz("t0")}')
        else:
            if ops[1].type == ARM_OP_IMM and ops[1].imm > 255:
                raise ValueError('tst wide imm carry')
            body.append(f't0 = {x} & {y}; {nz("t0")}')
        return body, True

    if len(ops) == 2 and base_m not in ('mov', 'movs', 'mvn', 'mvns', 'movw', 'movt', 'uxtb', 'uxth', 'sxtb', 'sxth', 'rev'):
        x, y = d, val(i, ops[1], pc)  # two-operand form: rd = rd op y
    elif len(ops) == 3:
        x, y = val(i, ops[1], pc), val(i, ops[2], pc)
    else:
        x, y = None, val(i, ops[1], pc) if len(ops) > 1 else None

    def setd(expr):
        if d == 'pc':
            raise ValueError('write pc')
        return f'{d} = {expr};'

    if base_m in ('add', 'adds', 'addw'):
        if d == 'pc':
            body.append(f't1 = {x} + {y};')
            body += jump_reg('t1', a, e)
            return body, False
        if S:
            body.append(f't0 = {x}; t1 = {y}; w = (uint64_t)t0 + t1; cf = (uint32_t)(w >> 32); vf = ((t0 ^ (uint32_t)w) & (t1 ^ (uint32_t)w)) >> 31; {d} = (uint32_t)w; {nz(d)}')
        else:
            body.append(setd(f'{x} + {y}'))
        return body, True
    if base_m in ('sub', 'subs', 'subw'):
        if S:
            body.append(f't0 = {x}; t1 = {y}; w = (uint64_t)t0 - t1; cf = t0 >= t1; vf = ((t0 ^ t1) & (t0 ^ (uint32_t)w)) >> 31; {d} = (uint32_t)w; {nz(d)}')
        else:
            body.append(setd(f'{x} - {y}'))
        return body, True
    if base_m in ('rsb', 'rsbs'):  # rsbs rd, rn, #0  (neg)
        if S:
            body.append(f't0 = {y}; t1 = {x}; w = (uint64_t)t0 - t1; cf = t0 >= t1; vf = ((t0 ^ t1) & (t0 ^ (uint32_t)w)) >> 31; {d} = (uint32_t)w; {nz(d)}')
        else:
            body.append(setd(f'{y} - {x}'))
        return body, True
    if base_m in ('adc', 'adcs'):
        body.append(f't0 = {x}; t1 = {y}; w = (uint64_t)t0 + t1 + cf;')
        if S:
            body.append(f'cf = (uint32_t)(w >> 32); vf = ((t0 ^ (uint32_t)w) & (t1 ^ (uint32_t)w)) >> 31;')
        body.append(f'{d} = (uint32_t)w;' + (nz(d) if S else ''))
        return body, True
    if base_m in ('sbc', 'sbcs'):
        body.append(f't0 = {x}; t1 = {y}; w = (uint64_t)t0 - t1 - (1 - cf);')
        if S:
            body.append(f'cf = !((w >> 32) & 1); vf = ((t0 ^ t1) & (t0 ^ (uint32_t)w)) >> 31;')
        body.append(f'{d} = (uint32_t)w;' + (nz(d) if S else ''))
        return body, True
    if base_m in ('mul', 'muls'):
        body.append(setd(f'{x} * {y}') + (nz(d) if S else ''))
        return body, True
    LOG = {'and': '&', 'orr': '|', 'eor': '^', 'bic': '& ~'}
    for k, opx in LOG.items():
        if base_m in (k, k + 's'):
            if S and ops[-1].type == ARM_OP_IMM and ops[-1].imm > 255:
                raise ValueError('logical wide imm carry')
            if S and ops[-1].type == ARM_OP_REG and ops[-1].shift.type != ARM_SFT_INVALID and ops[-1].shift.value:
                raise ValueError('logical shifted carry')
            body.append(setd(f'{x} {opx} {y}') + (nz(d) if S else ''))
            return body, True
    if base_m in ('mov', 'movs', 'mvn', 'mvns'):
        src = val(i, ops[1], pc)
        if ops[1].type == ARM_OP_REG and ops[1].shift.type != ARM_SFT_INVALID and ops[1].shift.value and S:
            raise ValueError('movs shifted')
        if S and ops[1].type == ARM_OP_IMM and ops[1].imm > 255:
            raise ValueError('movs wide imm')
        if base_m.startswith('mvn'):
            src = f'~{src}'
        if d == 'pc':
            body.append(f't1 = {src};')
            body += jump_reg('t1', a, e)
            return body, False
        body.append(setd(src) + (nz(d) if S else ''))
        return body, True
    if base_m == 'movw':
        return [setd(f'0x{ops[1].imm:x}u')], True
    if base_m == 'movt':
        return [setd(f'({d} & 0xffffu) | (0x{ops[1].imm:x}u << 16)')], True
    if base_m in ('uxtb', 'uxth', 'sxtb', 'sxth'):
        s_ = R(1)
        body.append(setd({'uxtb': f'{s_} & 0xffu', 'uxth': f'{s_} & 0xffffu',
                          'sxtb': f'(uint32_t)(int32_t)(int8_t){s_}', 'sxth': f'(uint32_t)(int32_t)(int16_t){s_}'}[base_m]))
        return body, True
    if base_m in ('lsl', 'lsls', 'lsr', 'lsrs', 'asr', 'asrs', 'ror', 'rors'):
        kind = base_m[:3]
        if len(ops) == 3:
            src, amt = val(i, ops[1], pc), ops[2]
        else:
            src, amt = d, ops[1]
        if amt.type == ARM_OP_IMM:
            k = amt.imm
            if kind == 'lsl':
                if k == 0:
                    body.append(setd(src) + (nz(d) if S else ''))
                else:
                    if S: body.append(f'cf = ({src} >> {32 - k}) & 1;')
                    body.append(setd(f'{src} << {k}') + (nz(d) if S else ''))
            elif kind == 'lsr':
                if S: body.append(f'cf = ({src} >> {k - 1}) & 1;')
                body.append(setd('0' if k == 32 else f'{src} >> {k}') + (nz(d) if S else ''))
            elif kind == 'asr':
                if S: body.append(f'cf = ((uint32_t)((int32_t){src} >> {min(k - 1, 31)})) & 1;')
                body.append(setd(f'(uint32_t)((int32_t){src} >> {min(k, 31)})') + (nz(d) if S else ''))
            else:
                body.append(setd(f'ror32({src}, {k})'))
                if S: body.append(f'cf = {d} >> 31; ' + nz(d))
            return body, True
        amt_r = reg(i, amt)
        body.append(f't0 = {src}; t1 = {amt_r} & 0xff;')
        fn = {'lsl': 'sh_lsl', 'lsr': 'sh_lsr', 'asr': 'sh_asr', 'ror': 'sh_ror'}[kind]
        if S:
            body.append(f'{d} = {fn}(t0, t1, &cf); {nz(d)}')
        else:
            body.append(f'{d} = {fn}(t0, t1, &t0);')
        return body, True
    if base_m == 'nop':
        return ['/* nop */'], True
    if base_m in ('svc', 'bkpt', 'udf'):
        return [f'rc_trap(c, 0x{a:x}, "{base_m}"); return;'], False
    raise ValueError('unsupported ' + m + ' ' + i.op_str)


def translate_safe(i, e, labels):
    try:
        return _translate(i, e, labels)
    except ValueError as ex:
        unsupported[str(ex).split(' ')[0] + ' ' + i.mnemonic] += 1
        return [f'rc_trap(c, 0x{i.address:x}, "unsupported: {i.mnemonic} {i.op_str}"); return;'], False


_translate = translate
translate = translate_safe


def main():
    os.makedirs(OUT, exist_ok=True)
    ents = sorted(entries)
    print('functions:', len(ents))
    chunk = []
    files = []
    size = 0
    nfile = 0

    def flush():
        nonlocal chunk, size, nfile
        if not chunk:
            return
        p = os.path.join(OUT, f'gen_{nfile:03d}.c')
        with open(p, 'w', newline='\n') as f:
            f.write('/* generated by tools/recomp.py from libtar-android.so - do not edit */\n#include "rc.h"\n#include "gen.h"\n\n')
            f.write('\n'.join(chunk))
            f.write('\n')
        files.append(p)
        nfile += 1
        chunk = []
        size = 0

    for e in ents:
        lines = emit_function(e)
        chunk.append('\n'.join(lines) + '\n')
        size += sum(len(x) for x in lines)
        if size > 1_500_000:
            flush()
    flush()
    with open(os.path.join(OUT, 'gen.h'), 'w', newline='\n') as f:
        f.write('/* generated */\n#pragma once\n#include "rc.h"\n')
        for e in ents:
            f.write(f'void {fname(e)}(Cpu *c);\n')
            if e in HOOKS:
                f.write(f'void {fname(e)}_orig(Cpu *c);\n')
        for nm in sorted(set(undef_names) | import_used):
            f.write(f'void imp_{nm}(Cpu *c);\n')
    with open(os.path.join(OUT, 'functab.inc'), 'w', newline='\n') as f:
        f.write('/* generated: (vaddr, function, name) of every recompiled function */\n')
        for e in ents:
            nm = (names.get(e) or gfuncs.get(e, '')).replace('\\', '\\\\').replace('"', '\\"')
            f.write(f'{{0x{e:x}u, {fname(e)}, "{nm[:120]}"}},\n')
    with open(os.path.join(OUT, 'imports.inc'), 'w', newline='\n') as f:
        f.write('/* generated: every undefined dynamic symbol of libtar-android.so */\n')
        for nm in undef_names:
            f.write(f'IMPORT({nm})\n')
    print('unsupported:', unsupported.most_common(40))
    print('files:', len(files))
    print('recovered switches:', sum(1 for v in _sw_cache.values() if v), 'unresolved computed jumps:', sorted(hex(x) for x in unresolved_jumps))


if __name__ == '__main__':
    main()

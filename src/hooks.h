/* Original functions wrapped by host code (hooks.c, ui/autoform.cpp, ui/capture.cpp).
 * The wrapper is f_<vaddr>(Cpu *c); it reaches the original as f_<vaddr>_orig(c).
 *  - recompiled build: tools/recomp.py generates f_<vaddr>_orig (src/gen) for its HOOKS, checked against this list
 *  - interpreter build (EH_INTERP, web): the interpreter calls the wrapper whenever control enters <vaddr>, and
 *    f_<vaddr>_orig (loader.c) runs the original there (rc_orig)
 * Addresses are ELF vaddrs (Ghidra = vaddr + 0x10000). */
#pragma once
#include "rc.h"

#define RC_HOOKS(X) \
    X(0a97cc) \
    X(0a99ec) \
    X(0b3c04) \
    X(0b421c) \
    X(0bce00) \
    X(0c1384) \
    X(0c1510) \
    X(0c34cc) \
    X(0c37c8) \
    X(0c5b48) \
    X(0c79a4) \
    X(0d0af4) \
    X(0d1a50) \
    X(0e8834) \
    X(0edacc) \
    X(0f4014) \
    X(10027c) \
    X(1100fc) \
    X(112cd8) \
    X(113e90) \
    X(117884) \
    X(118de0) \
    X(11a45c) \
    X(11bb48) \
    X(11be54) \
    X(11cb20) \
    X(11d154) \
    X(11d44c) \
    X(11e574) \
    X(11ff0c) \
    X(12273c) \
    X(13285c) \
    X(134904) \
    X(1350a4) \
    X(135214) \
    X(1352ac) \
    X(1352e4) \
    X(13f364) \
    X(141b84) \
    X(141c14) \
    X(141ccc) \
    X(1422bc) \
    X(142d40) \
    X(1437d0) \
    X(1437f0) \
    X(144608) \
    X(14958c) \
    X(157368) \
    X(157720) \
    X(15b8dc) \
    X(15c82c) \
    X(15c9d0) \
    X(19d900) \


#ifdef EH_INTERP
#ifdef __cplusplus
extern "C" {
#endif
void rc_orig(Cpu *c, uint32_t va);
#define RC_DECL_HOOK(v) void f_##v(Cpu *c); void f_##v##_orig(Cpu *c);
RC_HOOKS(RC_DECL_HOOK)
#undef RC_DECL_HOOK
#ifdef __cplusplus
}
#endif
#else
#include "gen/gen.h"
#endif

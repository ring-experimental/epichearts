//Find functions by symbol-name substring (mangled or demangled) and decompile them.
//Args: one or more substrings, e.g. FUNTER_ENC_Decrypt cflCrypt::Decrypt cImageFile
//@category EpicHearts

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

import java.io.File;
import java.io.FileWriter;
import java.util.*;

public class DecompileEpic4 extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] all = getScriptArgs(); String[] pats = java.util.Arrays.copyOfRange(all, 1, all.length);
		String outPath = all[0];

		// 1) collect matching symbols (full name = namespace::name)
		Map<String, Address> found = new LinkedHashMap<>();
		SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
		while (it.hasNext()) {
			Symbol s = it.next();
			String nm = s.getName();
			Namespace ns = s.getParentNamespace();
			String nsName = ns != null ? ns.getName() : "";
			String full = nsName.length() > 0 && !nsName.equals("GLOBAL")
					? nsName + "::" + nm : nm;
			for (String p : pats) {
				if (full.contains(p) || nm.contains(p)) {
					if (!found.containsKey(full + "@" + s.getAddress())) {
						found.put(full + "@" + s.getAddress(), s.getAddress());
					}
				}
			}
		}

		StringBuilder sb = new StringBuilder();
		sb.append("MATCHES (").append(found.size()).append("):\n");
		for (Map.Entry<String, Address> e : found.entrySet()) {
			sb.append("  ").append(e.getKey()).append(" @ ").append(e.getValue()).append("\n");
		}
		sb.append("\n");

		// 2) ensure function at address (try odd/even for Thumb), decompile
		DecompInterface dec = new DecompInterface();
		dec.setSimplificationStyle("decompile");
		dec.openProgram(currentProgram);
		AddressFactory af = currentProgram.getAddressFactory();

		for (Map.Entry<String, Address> e : found.entrySet()) {
			Address a = e.getValue();
			Function f = getFunctionAt(a);
			if (f == null && a.getOffset() % 2 == 1) {
				f = getFunctionAt(currentProgram.getAddressFactory()
						.getDefaultAddressSpace().getAddress(a.getOffset() & ~1L));
			}
			if (f == null) {
				try {
					f = createFunction(a, null);
				}
				catch (Exception ex) {
					// fall through
				}
			}
			sb.append("=== ").append(e.getKey()).append(" -> ")
					.append(f != null ? f.getName() + " @ " + f.getEntryPoint()
							: "NO FUNCTION")
					.append(" ===\n");
			if (f != null) {
				DecompileResults res = dec.decompileFunction(f, 90, monitor);
				if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
					sb.append(res.getDecompiledFunction().getC());
				}
				else {
					sb.append("DECOMPILE FAILED: ").append(res.getErrorMessage());
				}
			}
			sb.append("\n\n");
		}
		dec.dispose();

		File out = new File(outPath);
		if (out.exists()) {
			out.delete();
		}
		FileWriter w = new FileWriter(out, true);
		w.write(sb.toString());
		w.close();
		println("Wrote " + outPath + " (" + out.length() + " bytes), matches=" + found.size());
	}
}
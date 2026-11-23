//Decompile the callers (xrefs) of given symbol-name substrings.
//Args: substrings e.g. FUNTER_ENC_Decrypt
//@category EpicHearts

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

import java.io.File;
import java.io.FileWriter;
import java.util.*;

public class DecompileXrefs extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] pats = getScriptArgs();
		// repository root = the folder above ghidra_scripts/ (no machine-specific path)
		String outPath = new java.io.File(getSourceFile().getParentFile().getParentFile().getAbsolutePath(), "ghidra_xrefs.txt").getPath();

		// find target symbol addresses
		Map<String, Address> targets = new LinkedHashMap<>();
		SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
		while (it.hasNext()) {
			Symbol s = it.next();
			String nm = s.getName();
			for (String p : pats) {
				if (nm.contains(p)) {
					targets.putIfAbsent(nm + "@" + s.getAddress(), s.getAddress());
				}
			}
		}

		StringBuilder sb = new StringBuilder();
		DecompInterface dec = new DecompInterface();
		dec.setSimplificationStyle("decompile");
		dec.openProgram(currentProgram);

		Set<Address> callerDone = new LinkedHashSet<>();
		for (Map.Entry<String, Address> t : targets.entrySet()) {
			sb.append("### TARGET ").append(t.getKey()).append("\n");
			ReferenceIterator refs = currentProgram.getReferenceManager()
					.getReferencesTo(t.getValue());
			int n = 0;
			while (refs.hasNext()) {
				Reference r = refs.next();
				Address from = r.getFromAddress();
				sb.append("  ref from ").append(from).append(" type=").append(r.getReferenceType()).append("\n");
				n++;
				if (callerDone.contains(from)) {
					continue;
				}
				Function cf = getFunctionContaining(from);
				if (cf == null) {
					continue;
				}
				callerDone.add(cf.getEntryPoint());
				sb.append("  --- caller ").append(cf.getName()).append(" @ ")
						.append(cf.getEntryPoint()).append(" ---\n");
				DecompileResults res = dec.decompileFunction(cf, 90, monitor);
				if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
					sb.append(res.getDecompiledFunction().getC()).append("\n");
				}
				else {
					sb.append("DECOMPILE FAILED\n");
				}
			}
			if (n == 0) {
				sb.append("  NO REFS\n");
			}
			sb.append("\n");
		}
		dec.dispose();

		File out = new File(outPath);
		if (out.exists()) {
			out.delete();
		}
		FileWriter w = new FileWriter(out, true);
		w.write(sb.toString());
		w.close();
		println("Wrote " + outPath + " (" + out.length() + " bytes)");
	}
}
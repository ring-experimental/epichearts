//Find defined string data containing given substrings; decompile functions referencing them.
//Args: string substrings e.g. epic%d.png fttcustom
//@category EpicHearts

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

import java.io.File;
import java.io.FileWriter;
import java.util.*;

public class DecompileStrings extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] pats = getScriptArgs();
		// repository root = the folder above ghidra_scripts/ (no machine-specific path)
		String outPath = new java.io.File(getSourceFile().getParentFile().getParentFile().getAbsolutePath(), "ghidra_strings.txt").getPath();
		StringBuilder sb = new StringBuilder();
		DecompInterface dec = new DecompInterface();
		dec.setSimplificationStyle("decompile");
		dec.openProgram(currentProgram);
		Set<Address> done = new LinkedHashSet<>();

		DataIterator di = currentProgram.getListing().getDefinedData(true);
		while (di.hasNext()) {
			Data d = di.next();
			String repr;
			try {
				repr = d.getDefaultValueRepresentation();
			}
			catch (Exception ex) {
				continue;
			}
			boolean hit = false;
			for (String p : pats) {
				if (repr.contains(p)) {
					hit = true;
				}
			}
			if (!hit) {
				continue;
			}
			sb.append("### STRING ").append(repr).append(" @ ").append(d.getAddress()).append("\n");
			ReferenceIterator refs = currentProgram.getReferenceManager()
					.getReferencesTo(d.getAddress());
			int n = 0;
			while (refs.hasNext()) {
				Reference r = refs.next();
				Address from = r.getFromAddress();
				sb.append("  ref from ").append(from).append(" ").append(r.getReferenceType()).append("\n");
				n++;
				Function cf = getFunctionContaining(from);
				if (cf == null || done.contains(cf.getEntryPoint())) {
					continue;
				}
				done.add(cf.getEntryPoint());
				sb.append("  --- caller ").append(cf.getName()).append(" @ ")
						.append(cf.getEntryPoint()).append(" ---\n");
				DecompileResults res = dec.decompileFunction(cf, 90, monitor);
				if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
					sb.append(res.getDecompiledFunction().getC()).append("\n");
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
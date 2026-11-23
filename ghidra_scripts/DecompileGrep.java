//Decompile every function and keep those whose C text contains any of the given substrings.
//Args: substrings, e.g. "+ 0x158))"
//@category EpicHearts

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;

import java.io.FileWriter;

public class DecompileGrep extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] pats = getScriptArgs();
		DecompInterface dec = new DecompInterface();
		dec.setSimplificationStyle("decompile");
		dec.openProgram(currentProgram);
		StringBuilder sb = new StringBuilder();
		int n = 0, hits = 0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			n++;
			DecompileResults r = dec.decompileFunction(f, 20, monitor);
			if (r == null || !r.decompileCompleted()) continue;
			String c = r.getDecompiledFunction().getC();
			for (String p : pats) {
				if (c.contains(p)) {
					sb.append("=== ").append(f.getName(true)).append(" @ ").append(f.getEntryPoint()).append(" [").append(p).append("] ===\n").append(c).append("\n");
					hits++;
					break;
				}
			}
		}
		try (FileWriter w = new FileWriter(new java.io.File(getSourceFile().getParentFile().getParentFile().getAbsolutePath(), "ghidra_grep.txt").getPath())) { w.write(sb.toString()); }
		println("scanned " + n + " functions, hits " + hits);
	}
}

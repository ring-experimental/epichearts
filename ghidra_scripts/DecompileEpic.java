//Decompile selected functions by mangled name or hex address (0x...) and write to ghidra_decomp.txt in the repository root (the folder above ghidra_scripts/)
//@category EpicHearts

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;

import java.io.File;
import java.io.FileWriter;

public class DecompileEpic extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		// repository root = the folder above ghidra_scripts/ (no machine-specific path)
		String outPath = new java.io.File(getSourceFile().getParentFile().getParentFile().getAbsolutePath(), "ghidra_decomp.txt").getPath();
		StringBuilder sb = new StringBuilder();
		DecompInterface dec = new DecompInterface();
		dec.setSimplificationStyle("decompile");
		dec.openProgram(currentProgram);

		for (String a : args) {
			Function f = null;
			try {
				if (a.startsWith("0x") || a.startsWith("0X")) {
					long addr = Long.parseLong(a.substring(2), 16);
					Address ad = currentProgram.getAddressFactory()
							.getDefaultAddressSpace().getAddress(addr);
					f = getFunctionAt(ad);
					if (f == null) {
						f = createFunction(ad, null);
					}
				}
				else {
					java.util.List<Function> fs = getGlobalFunctions(a);
					if (fs != null && !fs.isEmpty()) {
						f = fs.get(0);
					}
				}
			}
			catch (Exception e) {
				sb.append("=== ").append(a).append(" -> ERROR ").append(e).append("\n\n");
				continue;
			}
			sb.append("=== ").append(a).append(" -> ")
					.append(f != null ? f.getName() + " @ " + f.getEntryPoint()
							: "NOT FOUND")
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
		println("Wrote " + outPath + " (" + out.length() + " bytes)");
	}
}
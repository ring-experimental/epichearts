//Decompile every function and write them grouped by namespace (class) into work/decomp/<ns>.c
//Args: output directory
//@category EpicHearts

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

import java.io.File;
import java.io.FileWriter;
import java.util.*;

public class DumpAll extends GhidraScript {
	@Override
	public void run() throws Exception {
		String outDir = getScriptArgs()[0];
		new File(outDir).mkdirs();
		DecompInterface dec = new DecompInterface();
		DecompileOptions opt = new DecompileOptions();
		dec.setOptions(opt);
		dec.toggleCCode(true);
		dec.setSimplificationStyle("decompile");
		dec.openProgram(currentProgram);
		Map<String, StringBuilder> byNs = new TreeMap<>();
		StringBuilder index = new StringBuilder();
		int n = 0, fail = 0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			if (monitor.isCancelled()) break;
			if (f.isThunk() || f.isExternal()) continue;
			Namespace ns = f.getParentNamespace();
			String nsName = (ns == null || ns.isGlobal()) ? "_global" : ns.getName(true).replace("::", "__");
			nsName = nsName.replaceAll("[^A-Za-z0-9_]", "_");
			if (nsName.length() > 100) nsName = nsName.substring(0, 100);
			long size = f.getBody().getNumAddresses();
			index.append(f.getEntryPoint()).append(' ').append(size).append(' ').append(nsName).append(' ').append(f.getName(true)).append('\n');
			DecompileResults r = dec.decompileFunction(f, 60, monitor);
			String c;
			if (r == null || !r.decompileCompleted()) { c = "/* DECOMPILE FAILED */\n"; fail++; }
			else c = r.getDecompiledFunction().getC();
			byNs.computeIfAbsent(nsName, k -> new StringBuilder())
				.append("// ==== ").append(f.getName(true)).append(" @ ").append(f.getEntryPoint())
				.append(" size=").append(size).append('\n').append(c).append('\n');
			n++;
			if (n % 500 == 0) println("decompiled " + n);
		}
		for (Map.Entry<String, StringBuilder> e : byNs.entrySet()) {
			try (FileWriter w = new FileWriter(new File(outDir, e.getKey() + ".c"))) { w.write(e.getValue().toString()); }
		}
		try (FileWriter w = new FileWriter(new File(outDir, "_index.txt"))) { w.write(index.toString()); }
		println("done " + n + " functions, failed " + fail);
	}
}

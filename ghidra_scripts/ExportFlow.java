//Export instruction addresses, function bodies and computed-jump targets for the static recompiler.
//Output lines:
//  F <entry> <name>            function entry (Ghidra address)
//  R <start> <end>             body range of the preceding function (inclusive)
//  J <from> <to> <to> ...      computed jump (switch) targets
//  C <from> <to> ...           computed call targets Ghidra knows (rare)
//  I <addr> <len>              every disassembled instruction (in all of .text)
//@category EpicHearts

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

import java.io.*;

public class ExportFlow extends GhidraScript {
	@Override
	public void run() throws Exception {
		String out = getScriptArgs()[0];
		try (PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)))) {
			Listing lst = currentProgram.getListing();
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				if (f.isThunk() || f.isExternal()) continue;
				w.println("F " + f.getEntryPoint() + " " + f.getName(true).replace(' ', '_'));
				for (AddressRange r : f.getBody()) w.println("R " + r.getMinAddress() + " " + r.getMaxAddress());
			}
			InstructionIterator it = lst.getInstructions(true);
			while (it.hasNext()) {
				Instruction ins = it.next();
				w.println("I " + ins.getAddress() + " " + ins.getLength());
				FlowType ft = ins.getFlowType();
				if (ft.isComputed()) {
					StringBuilder sb = new StringBuilder();
					for (Reference r : ins.getReferencesFrom()) {
						RefType rt = r.getReferenceType();
						if (rt.isFlow()) sb.append(' ').append(r.getToAddress());
					}
					w.println((ft.isCall() ? "C " : "J ") + ins.getAddress() + sb);
				}
			}
		}
		println("export done");
	}
}

// Every function of the program with what a matcher can use to find the same function in another build of the game: size, instruction count, the functions it
// calls, the strings it refers to, and the imports it calls. Used to map the 64-bit game's functions to the 32-bit ones (tools/match_funcs.py).
// Usage (headless): sh tools/ghidra/g.sh -s ExportFuncs.java <output file>
// One line per function: <rva hex> <size> <instructions> | <callee rva hex>,... | <string>\x1f<string>... | <import>,...   (rva = address - image base)
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class ExportFuncs extends GhidraScript {
	// A NUL-terminated printable ASCII string of 4+ characters at the address (whether or not Ghidra defined it as a string)
	private String stringAt(Address a) {
		try {
			if (!currentProgram.getMemory().contains(a))
				return null;
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < 200; i++) {
				int b = currentProgram.getMemory().getByte(a.add(i)) & 0xff;
				if (b == 0)
					break;
				if (b < 32 || b > 126)
					return null;
				sb.append((char) b);
			}
			return sb.length() >= 4 && sb.length() < 200 ? sb.toString() : null;
		} catch (Exception e) {
			return null;
		}
	}

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		long base = currentProgram.getImageBase().getOffset();
		try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext()) {
				Function f = it.next();
				if (f.isThunk())
					continue;
				Set<String> callees = new LinkedHashSet<>(), strings = new LinkedHashSet<>(), imports = new LinkedHashSet<>();
				int n = 0;
				InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
				while (ii.hasNext()) {
					Instruction in = ii.next();
					n++;
					for (Reference r : in.getReferencesFrom()) {
						Address to = r.getToAddress();
						if (r.getReferenceType().isCall()) {
							Function c = getFunctionAt(to);
							if (c != null && c.isThunk() && c.getThunkedFunction(true) != null)
								c = c.getThunkedFunction(true);
							if (c != null && c.isExternal())
								imports.add(c.getName());
							else if (c != null)
								callees.add(Long.toHexString(c.getEntryPoint().getOffset() - base));
							else {
								Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(to);
								if (s != null)
									imports.add(s.getName());
							}
						} else if (r.getReferenceType().isData()) {
							String s = stringAt(to);
							if (s != null)
								strings.add(s);
						}
					}
				}
				out.println(Long.toHexString(f.getEntryPoint().getOffset() - base) + " " + f.getBody().getNumAddresses() + " " + n + " | "
						+ String.join(",", callees) + " | " + String.join("\u001f", strings) + " | " + String.join(",", imports));
			}
		}
		println("done");
	}
}

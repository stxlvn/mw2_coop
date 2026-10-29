// List the instructions that use a given immediate (scalar) operand, e.g. a flag bit tested with TEST/AND, with their function.
// Arguments: <value hex> [mnemonic prefix filter, e.g. TEST or AND; "any" for all] [max hits, default 60].
// Usage: sh tools/ghidra/g.sh -s FindImm.java 0x4000000 TEST
// @category mw2coop
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

public class FindImm extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] a = getScriptArgs();
		long want = Long.parseUnsignedLong(a[0].replace("0x", ""), 16);
		String filter = a.length > 1 ? a[1].toUpperCase() : "ANY";
		int max = a.length > 2 ? Integer.parseInt(a[2]) : 60;
		long base = currentProgram.getImageBase().getOffset();
		int hits = 0;
		InstructionIterator it = currentProgram.getListing().getInstructions(true);
		while (it.hasNext() && hits < max && !monitor.isCancelled()) {
			Instruction ins = it.next();
			if (!filter.equals("ANY") && !ins.getMnemonicString().toUpperCase().startsWith(filter)) {
				continue;
			}
			boolean match = false;
			for (int op = 0; op < ins.getNumOperands() && !match; op++) {
				for (Object o : ins.getOpObjects(op)) {
					if (o instanceof Scalar && (((Scalar) o).getUnsignedValue() & 0xffffffffL) == want) {
						match = true;
					}
				}
			}
			if (!match) {
				continue;
			}
			Function f = getFunctionContaining(ins.getAddress());
			println("rva 0x" + Long.toHexString(ins.getAddress().getOffset() - base) + "  " + ins + "   in "
				+ (f == null ? "?" : f.getName() + " rva 0x" + Long.toHexString(f.getEntryPoint().getOffset() - base)));
			hits++;
		}
		println(hits + " hits");
	}
}

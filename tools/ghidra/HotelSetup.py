# Ghidra pre-script (Jython): set up cdi_hotel before auto-analysis.
# Args: <syms.txt from tools/stb.py> <cdi_hotel_data module>
#  - adds the OS-9 data area at DATA_BASE (a6 = DATA_BASE + 0x8000) with the initialised data
#    (incl. the _jmptbl jump table) from the cdi_hotel_data module
#  - creates a label/function for every symbol in the original .stb
#@category HotelMario
import struct

from ghidra.program.model.symbol import SourceType
from java.math import BigInteger

DATA_BASE = 0x100000
DATA_SIZE = 0x2600
EXEC = 0x52

args = getScriptArgs()
syms_path, data_mod_path = args[0], args[1]

space = currentProgram.getAddressFactory().getDefaultAddressSpace()
mem = currentProgram.getMemory()

# data area
blk = mem.createInitializedBlock("data", space.getAddress(DATA_BASE), DATA_SIZE, 0, monitor, False)
blk.setWrite(True)
dm = open(data_mod_path, "rb").read()
off, size = struct.unpack(">II", dm[0x44:0x4C])
mem.setBytes(space.getAddress(DATA_BASE + off), dm[0x4C:0x4C + size])

# a6 holds DATA_BASE + 0x8000 everywhere in the program
a6 = currentProgram.getRegister("A6")
code_end = space.getAddress(mem.getBlock("ram").getEnd().getOffset()) if mem.getBlock("ram") else space.getAddress(0x1B940)
currentProgram.getProgramContext().setValue(a6, space.getAddress(0), code_end, BigInteger.valueOf(DATA_BASE + 0x8000))

# original symbols
st = currentProgram.getSymbolTable()
nfunc = ndata = 0
for line in open(syms_path):
    if line.startswith("\xef\xbb\xbf"):  # UTF-8 BOM (e.g. from a PowerShell redirect)
        line = line[3:]
    parts = line.split()
    if len(parts) != 3:
        continue
    val, flags, name = int(parts[0], 16), int(parts[1], 16), parts[2]
    if flags & 0x4:
        if val < EXEC:
            continue
        addr = space.getAddress(val)
        st.createLabel(addr, name, SourceType.IMPORTED)
        if getFunctionAt(addr) is None:
            disassemble(addr)
            createFunction(addr, name)
        nfunc += 1
    else:
        signed = val - 0x100000000 if val & 0x80000000 else val
        addr = space.getAddress(DATA_BASE + 0x8000 + signed)
        if DATA_BASE <= addr.getOffset() < DATA_BASE + DATA_SIZE:
            st.createLabel(addr, name, SourceType.IMPORTED)
            ndata += 1
print("HotelSetup: %d functions, %d data symbols" % (nfunc, ndata))

# Ghidra post-script (Jython): give compiled functions the Microware C 3.x calling convention.
# Args: <asm/funcs dir>
#   arg1 -> D0, arg2 -> D1, arg3.. on the stack (+4, +8, ... from the entry SP), result in D0.
# The argument count comes from the asm: the prologue's movem saves d0/d1 when they are parameters,
# and stack parameters are read as positive offsets from a5 (8(a5) = arg3, $c(a5) = arg4 ...).
# Hand-written asm functions (no link.w a5) are left alone.
#@category HotelMario
import os
import re

from ghidra.program.model.data import IntegerDataType
from ghidra.program.model.listing import ParameterImpl, ReturnParameterImpl, VariableStorage
from ghidra.program.model.listing.Function import FunctionUpdateType
from ghidra.program.model.symbol import SourceType

asmdir = getScriptArgs()[0]
prog = currentProgram
D0, D1 = prog.getRegister("D0"), prog.getRegister("D1")
INT = IntegerDataType.dataType


def arg_count(path):
    text = open(path).read()
    lines = [l.split(";")[0].strip() for l in text.splitlines()]
    body = [l for l in lines if l and not l.endswith(":")]
    if not body or not body[0].startswith("link.w"):
        return None
    regs = 0
    if len(body) > 1 and body[1].startswith("movem.l") and body[1].endswith("-(a7)"):
        mask = body[1].split()[1].split(",")[0]
        if re.search(r"\bd0\b|d0-", mask):
            regs = 1
        if re.search(r"d0-d[1-7]|\bd1\b|d1-", mask):
            regs = 2
    offs = [int(m, 16) for m in re.findall(r"(?<![-\w])\$([0-9a-f]+)\(a5\)", text)]
    offs = [o for o in offs if o >= 8]
    stack = (max(offs) - 8) // 4 + 1 if offs else 0
    if stack:
        regs = 2  # stack arguments only exist once d0 and d1 are used
    return regs, stack


done = 0
for f in prog.getFunctionManager().getFunctions(True):
    path = os.path.join(asmdir, f.getName() + ".s")
    if not os.path.exists(path):
        continue
    cnt = arg_count(path)
    if cnt is None:
        continue
    regs, stack = cnt
    params = []
    for i, r in enumerate((D0, D1)[:regs]):
        params.append(ParameterImpl("arg%d" % (i + 1), INT, r, prog))
    for i in range(stack):
        params.append(ParameterImpl("arg%d" % (i + 3), INT, 4 + 4 * i, prog))
    ret = ReturnParameterImpl(INT, VariableStorage(prog, [D0]), prog)
    f.updateFunction(None, ret, params, FunctionUpdateType.CUSTOM_STORAGE, True, SourceType.USER_DEFINED)
    done += 1

# _jmptbl entries (jmp abs.l in the data area) become thunks of their targets, so calls through
# jsr -x(a6) show the real function name and parameters.
space = prog.getAddressFactory().getDefaultAddressSpace()
data = prog.getMemory().getBlock("data")
mem = prog.getMemory()
thunks = 0
a = data.getStart().getOffset()
end = data.getEnd().getOffset()
while a + 6 <= end:
    addr = space.getAddress(a)
    if mem.getShort(addr) & 0xFFFF == 0x4EF9:
        target = getFunctionAt(space.getAddress(mem.getInt(addr.add(2)) & 0xFFFFFFFF))
        if target is not None:
            f = getFunctionAt(addr)
            if f is None:
                disassemble(addr)
                f = createFunction(addr, None)
            if f is not None:
                f.setThunkedFunction(target)
                thunks += 1
        a += 6
    else:
        a += 2
print("ApplyCC: set parameters on %d functions, %d jump-table thunks" % (done, thunks))

# Ghidra post-script (Jython): write a decompiled C draft per function.
# Args: <out_dir> [function names...]   (no names = every function)
# Drafts are a starting point only: check every one against asm/funcs/<name>.s.
#@category HotelMario
import os

from ghidra.app.decompiler import DecompInterface

args = list(getScriptArgs())
outdir = args[0]
only = set(args[1:])
if not os.path.isdir(outdir):
    os.makedirs(outdir)

dec = DecompInterface()
dec.openProgram(currentProgram)
n = 0
for f in currentProgram.getFunctionManager().getFunctions(True):
    name = f.getName()
    if only and name not in only:
        continue
    res = dec.decompileFunction(f, 60, monitor)
    if not res.decompileCompleted():
        continue
    out = open(os.path.join(outdir, name + ".c"), "w")
    out.write("/* Ghidra draft of %s @ 0x%x - verify against asm/funcs/%s.s */\n" % (name, f.getEntryPoint().getOffset(), name))
    out.write(res.getDecompiledFunction().getC())
    out.close()
    n += 1
print("ExportC: wrote %d drafts to %s" % (n, outdir))

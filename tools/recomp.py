"""Static recompiler: translate every function of cdi_hotel (68000) into C for the port.

Usage: python tools/recomp.py [out_dir]          (default build/recomp)

Reads the program and symbols extracted from YOUR disc (build/eu/fs) and writes C that runs on top
of port/m68k.h. Output is generated, never committed. Instructions it can't translate become
unimpl() calls, and the summary lists them so coverage can be improved.
"""
import collections
import os
import struct
import sys

import capstone
from capstone import m68k_const as K

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from stb import parse  # noqa: E402

FS = os.path.join("build", "eu", "fs")
CODE_FLAG = 0x4

CC = {"t": "1", "f": "0", "hi": "(!cpu.c && !cpu.z)", "ls": "(cpu.c || cpu.z)", "cc": "(!cpu.c)", "hs": "(!cpu.c)",
      "cs": "(cpu.c)", "lo": "(cpu.c)", "ne": "(!cpu.z)", "eq": "(cpu.z)", "vc": "(!cpu.v)", "vs": "(cpu.v)",
      "pl": "(!cpu.n)", "mi": "(cpu.n)", "ge": "(cpu.n == cpu.v)", "lt": "(cpu.n != cpu.v)",
      "gt": "(!cpu.z && cpu.n == cpu.v)", "le": "(cpu.z || cpu.n != cpu.v)", "ra": "1"}


class Unsupported(Exception):
    pass


def reg(rid):
    if 1 <= rid <= 8:
        return "d", rid - 1
    if 9 <= rid <= 16:
        return "a", rid - 9
    raise Unsupported(f"reg {rid}")


def regexpr(rid):
    k, n = reg(rid)
    return f"cpu.{k}[{n}]"


class EA:
    """One operand: `pre` statements to run first, then read()/write(v) expressions."""

    def __init__(self, i, op, sz, opnum):
        self.pre, self.kind, self.sz = [], None, sz
        am = op.address_mode
        tmp = f"ea{opnum}"
        if am == K.M68K_AM_REG_DIRECT_DATA:
            self.kind, self.r = "d", regexpr(op.reg)
        elif am == K.M68K_AM_REG_DIRECT_ADDR:
            self.kind, self.r = "a", regexpr(op.reg)
        elif am == K.M68K_AM_IMMEDIATE:
            self.kind, self.val = "imm", op.imm & 0xFFFFFFFF
        else:
            self.kind = "mem"
            m = op.mem
            base_reg = m.base_reg or op.reg  # (An), (An)+ and -(An) carry the register in op.reg
            if am == K.M68K_AM_REGI_ADDR:
                addr = regexpr(base_reg)
            elif am == K.M68K_AM_REGI_ADDR_POST_INC:
                r = regexpr(base_reg)
                inc = 2 if (sz == 1 and r == "cpu.a[7]") else sz
                self.pre.append(f"uint32_t {tmp} = {r}; {r} += {inc};")
                addr = tmp
            elif am == K.M68K_AM_REGI_ADDR_PRE_DEC:
                r = regexpr(base_reg)
                dec = 2 if (sz == 1 and r == "cpu.a[7]") else sz
                self.pre.append(f"{r} -= {dec}; uint32_t {tmp} = {r};")
                addr = tmp
            elif am == K.M68K_AM_REGI_ADDR_DISP:
                addr = f"({regexpr(base_reg)} + {m.disp})"
            elif am in (K.M68K_AM_AREGI_INDEX_8_BIT_DISP, K.M68K_AM_AREGI_INDEX_BASE_DISP):
                idx = regexpr(m.index_reg)
                idx = idx if m.index_size == 1 else f"sx({idx}, 2)"
                addr = f"({regexpr(base_reg)} + {m.disp} + {idx})"
            elif am == K.M68K_AM_PCI_DISP:
                addr = f"0x{(i.address + 2 + m.disp) & 0xFFFFFFFF:x}u"
            elif am in (K.M68K_AM_PCI_INDEX_8_BIT_DISP, K.M68K_AM_PCI_INDEX_BASE_DISP):
                idx = regexpr(m.index_reg)
                idx = idx if m.index_size == 1 else f"sx({idx}, 2)"
                addr = f"(0x{(i.address + 2 + m.disp) & 0xFFFFFFFF:x}u + {idx})"
            elif am == K.M68K_AM_ABSOLUTE_DATA_SHORT:
                addr = f"0x{(op.imm if op.imm < 0x8000 else op.imm | 0xFFFF0000) & 0xFFFFFFFF:x}u"
            elif am == K.M68K_AM_ABSOLUTE_DATA_LONG:
                addr = f"0x{op.imm & 0xFFFFFFFF:x}u"
            else:
                raise Unsupported(f"addressing mode {am}")
            if am not in (K.M68K_AM_REGI_ADDR_POST_INC, K.M68K_AM_REGI_ADDR_PRE_DEC):
                self.pre.append(f"uint32_t {tmp} = {addr};")
            self.addr = tmp

    def read(self, sz=None):
        sz = sz or self.sz
        if self.kind in ("d", "a"):
            return f"({self.r} & 0x{0xFFFFFFFF if sz == 4 else (1 << sz * 8) - 1:x}u)"
        if self.kind == "imm":
            return f"0x{self.val & ((1 << sz * 8) - 1):x}u"
        return f"rdsz({self.addr}, {sz})"

    def write(self, v, sz=None):
        sz = sz or self.sz
        if self.kind == "d":
            return f"setsz(&{self.r}, {v}, {sz});"
        if self.kind == "a":
            return f"{self.r} = sx({v}, {sz});"
        if self.kind == "mem":
            return f"wrsz({self.addr}, {v}, {sz});"
        raise Unsupported("write to immediate")


class Func:
    def __init__(self, name, start, end, insns, all_funcs):
        self.name, self.start, self.end, self.insns = name, start, end, insns
        self.funcs = all_funcs
        self.addrs = {i.address for i in insns}
        self.labels = set()
        self.dynamic_jump = False
        self.missing = collections.Counter()

    def target(self, i):
        return (i.address + 2 + i.operands[0].br_disp.disp) & 0xFFFFFFFF

    def branch_to(self, t, cond="1"):
        """goto inside this function, tail-call another function, or give up."""
        if t in self.addrs and self.start <= t < self.end:
            self.labels.add(t)
            return f"if ({cond}) goto L_{t:x};"
        if t in self.funcs:
            return f"if ({cond}) {{ {cfn(t)}(); return; }}"
        self.missing["branch outside function"] += 1
        return f"if ({cond}) unimpl(0x{t:x}, \"branch outside function\");"

    def call(self, i, t):
        ret = i.address + i.size
        if t in self.funcs:
            return f"PUSH32(0x{ret:x}); {cfn(t)}();"
        self.missing["bsr into function body"] += 1
        return f"unimpl(0x{t:x}, \"bsr into function body\");"

    def translate(self, i, nxt):
        mn = i.mnemonic
        base, _, suf = mn.partition(".")
        sz = {"b": 1, "w": 2, "l": 4}.get(suf, 4)
        ops = list(i.operands)
        out = []

        def ea(k, size=sz):
            e = EA(i, ops[k], size, k)
            out.extend(e.pre)
            return e

        if base in ("move", "movea"):
            s = ea(0)
            d = ea(1)
            v = f"v{i.address:x}"
            out.append(f"uint32_t {v} = {s.read()};")
            if base == "movea":
                out.append(d.write(v))
            else:
                out.append(d.write(v))
                out.append(f"f_nz({v}, {sz});")
        elif base == "moveq":
            d = ea(1)
            out.append(f"{d.r} = 0x{ops[0].imm & 0xFFFFFFFF:x}u; f_nz({d.r}, 4);")
        elif base == "lea":
            s = EA(i, ops[0], 4, 0)
            out.extend(s.pre)
            out.append(f"{regexpr(ops[1].reg)} = {s.addr};")
        elif base == "pea":
            s = EA(i, ops[0], 4, 0)
            out.extend(s.pre)
            out.append(f"PUSH32({s.addr});")
        elif base == "clr":
            d = ea(0)
            out.append(d.write("0"))
            out.append("cpu.n = cpu.v = cpu.c = 0; cpu.z = 1;")
        elif base == "tst":
            s = ea(0)
            out.append(f"f_nz({s.read()}, {sz});")
        elif base in ("cmp", "cmpi", "cmpa", "cmpm"):
            s, d = ea(0), ea(1)
            if base == "cmpa":
                out.append(f"f_sub(sx({s.read()}, {sz}), {d.read(4)}, 4, 0, 0);")
            else:
                out.append(f"f_sub({s.read()}, {d.read()}, {sz}, 0, 0);")
        elif base in ("add", "addi", "addq", "adda", "sub", "subi", "subq", "suba"):
            s, d = ea(0), ea(1)
            add = base.startswith("add")
            if base in ("adda", "suba") or d.kind == "a":
                src = f"sx({s.read()}, {sz})" if base in ("adda", "suba") else s.read()
                out.append(f"{d.r} {'+' if add else '-'}= {src};")
            else:
                f = "f_add" if add else "f_sub"
                v = f"v{i.address:x}"
                out.append(f"uint32_t {v} = {f}({s.read()}, {d.read()}, {sz}, 0, 1);")
                out.append(d.write(v))
        elif base in ("addx", "subx"):
            s, d = ea(0), ea(1)
            v = f"v{i.address:x}"
            out.append(f"uint32_t {v} = f_{base}({s.read()}, {d.read()}, {sz});")
            out.append(d.write(v))
        elif base in ("and", "andi", "or", "ori", "eor", "eori"):
            op = {"a": "&", "o": "|", "e": "^"}[base[0]]
            s, d = ea(0), ea(1)
            v = f"v{i.address:x}"
            out.append(f"uint32_t {v} = {d.read()} {op} {s.read()};")
            out.append(d.write(v))
            out.append(f"f_nz({v}, {sz});")
        elif base in ("neg", "negx", "not"):
            d = ea(0)
            v = f"v{i.address:x}"
            if base == "not":
                out.append(f"uint32_t {v} = ~{d.read()};")
                out.append(d.write(v))
                out.append(f"f_nz({v}, {sz});")
            else:
                f = "f_sub" if base == "neg" else "f_subx"
                args = f"{d.read()}, 0, {sz}, 0, 1" if base == "neg" else f"{d.read()}, 0, {sz}"
                out.append(f"uint32_t {v} = {f}({args});")
                out.append(d.write(v))
        elif base == "ext":
            r = regexpr(ops[0].reg)
            if sz == 2:
                out.append(f"setsz(&{r}, sx({r}, 1), 2); f_nz({r}, 2);")
            else:
                out.append(f"{r} = sx({r}, 2); f_nz({r}, 4);")
        elif base == "swap":
            r = regexpr(ops[0].reg)
            out.append(f"{r} = ({r} << 16) | ({r} >> 16); f_nz({r}, 4);")
        elif base == "exg":
            a, b = regexpr(ops[0].reg), regexpr(ops[1].reg)
            out.append(f"{{ uint32_t t = {a}; {a} = {b}; {b} = t; }}")
        elif base in ("lsl", "lsr", "asl", "asr", "rol", "ror", "roxl", "roxr"):
            if len(ops) == 1:  # memory form, shift by one
                d = ea(0, 2)
                cnt, sz = "1", 2
            else:
                d = ea(1)
                c = ops[0]
                cnt = str(c.imm) if c.address_mode == K.M68K_AM_IMMEDIATE else f"({regexpr(c.reg)} & 63)"
            v = f"v{i.address:x}"
            out.append(f"uint32_t {v} = f_{base}({d.read()}, {cnt}, {sz});")
            out.append(d.write(v))
        elif base in ("btst", "bset", "bclr", "bchg"):
            d = EA(i, ops[1], 1, 1)
            out.extend(d.pre)
            width = 32 if d.kind == "d" else 8
            bit = str(ops[0].imm % width) if ops[0].address_mode == K.M68K_AM_IMMEDIATE else f"({regexpr(ops[0].reg)} % {width})"
            dsz = 4 if d.kind == "d" else 1
            v = f"v{i.address:x}"
            out.append(f"uint32_t {v} = {d.read(dsz)}; cpu.z = !(({v} >> {bit}) & 1);")
            if base != "btst":
                op = {"bset": f"{v} | (1u << {bit})", "bclr": f"{v} & ~(1u << {bit})", "bchg": f"{v} ^ (1u << {bit})"}[base]
                out.append(d.write(op, dsz))
        elif base in ("mulu", "muls", "divu", "divs"):
            s = ea(0, 2)
            out.append(f"f_{base}({s.read(2)}, {reg(ops[1].reg)[1]});")
        elif base == "movem":
            out.extend(self.movem(i, sz))
        elif base == "link":
            r = regexpr(ops[0].reg)
            out.append(f"PUSH32({r}); {r} = cpu.a[7]; cpu.a[7] += {ops[1].imm};")
        elif base == "unlk":
            r = regexpr(ops[0].reg)
            out.append(f"cpu.a[7] = {r}; {r} = rd32(cpu.a[7]); cpu.a[7] += 4;")
        elif base == "rts":
            out.append("RTS();")
        elif base == "bra" or (base.startswith("b") and base[1:] in CC):
            out.append(self.branch_to(self.target(i), CC[base[1:]]))
        elif base.startswith("db") and base[2:] in CC:
            r = regexpr(ops[0].reg)
            t = (i.address + 2 + ops[1].br_disp.disp) & 0xFFFFFFFF
            out.append(f"if (!{CC[base[2:]]}) {{ uint16_t w = (uint16_t)({r} - 1); setsz(&{r}, w, 2); "
                       f"{self.branch_to(t, 'w != 0xFFFF')} }}")
        elif base.startswith("s") and base[1:] in CC:
            d = ea(0, 1)
            out.append(d.write(f"({CC[base[1:]]} ? 0xFF : 0)", 1))
        elif base == "bsr":
            out.append(self.call(i, self.target(i)))
        elif base in ("jsr", "jmp"):
            e = EA(i, ops[0], 4, 0)
            out.extend(e.pre)
            ret = i.address + i.size
            const = e.addr if False else None
            if base == "jsr":
                out.append(f"call_addr({e.addr}, 0x{ret:x});")
            else:
                self.dynamic_jump = True
                out.append(f"DYNJUMP({e.addr});")
        elif base == "trap":
            code = (nxt[0] << 8 | nxt[1]) if nxt else 0
            out.append(f"os9_trap({ops[0].imm}, 0x{code:x});")
        elif base == "nop":
            pass
        else:
            raise Unsupported(base)
        return out

    def movem(self, i, sz):
        o0, o1 = i.operands
        to_mem = o0.type == K.M68K_OP_REG_BITS or (o0.address_mode == K.M68K_AM_NONE)
        bits_op, mem_op = (o0, o1) if to_mem else (o1, o0)
        bits = bits_op.register_bits
        regs = [f"cpu.{'d' if b < 8 else 'a'}[{b % 8}]" for b in range(16) if bits >> b & 1]
        out = []
        am = mem_op.address_mode
        if to_mem and am == K.M68K_AM_REGI_ADDR_PRE_DEC:
            base = regexpr(mem_op.mem.base_reg or mem_op.reg)
            for r in reversed(regs):
                out.append(f"{base} -= {sz}; wrsz({base}, {r}, {sz});")
            return out
        if not to_mem and am == K.M68K_AM_REGI_ADDR_POST_INC:
            base = regexpr(mem_op.mem.base_reg or mem_op.reg)
            for r in regs:
                out.append(f"{r} = sx(rdsz({base}, {sz}), {sz}); {base} += {sz};")
            return out
        e = EA(i, mem_op, sz, 9)
        out.extend(e.pre)
        for k, r in enumerate(regs):
            a = f"{e.addr} + {k * sz}"
            out.append(f"wrsz({a}, {r}, {sz});" if to_mem else f"{r} = sx(rdsz({a}, {sz}), {sz});")
        return out

    def emit(self, d):
        body = []
        for k, i in enumerate(self.insns):
            nxt = d[i.address + 2:i.address + 4] if i.mnemonic == "trap" else None
            try:
                lines = self.translate(i, nxt)
            except Exception as ex:  # noqa: BLE001 - anything odd becomes unimpl()
                what = str(ex) if isinstance(ex, Unsupported) else type(ex).__name__
                self.missing[f"{i.mnemonic}: {what}"] += 1
                lines = [f"unimpl(0x{i.address:x}, \"{i.mnemonic} {i.op_str}\");"]
            body.append((i.address, i.mnemonic + " " + i.op_str, lines))
        out = [f"/* {self.name} @ 0x{self.start:x} */", f"void {cfn(self.start)}(void)", "{"]
        if self.dynamic_jump:
            self.labels |= self.addrs
            cases = " ".join(f"case 0x{a:x}: goto L_{a:x};" for a in sorted(self.addrs))
            out.append(f"#define DYNJUMP(t) do {{ switch (t) {{ {cases} default: unimpl(t, \"jump\"); return; }} }} while (0)")
        for addr, text, lines in body:
            if addr in self.labels:
                out.append(f"L_{addr:x}:;")
            out.append(f"    {{ /* {addr:06x} {text} */ " + " ".join(lines) + " }")
        out.append("    return;")
        if self.dynamic_jump:
            out.append("#undef DYNJUMP")
        out.append("}")
        return "\n".join(out)


def cfn(addr):
    return f"f_{addr:06x}"


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "recomp")
    os.makedirs(outdir, exist_ok=True)
    d = open(os.path.join(FS, "cdi_hotel"), "rb").read()
    exec_off = struct.unpack(">I", d[0x30:0x34])[0]
    code_end = struct.unpack(">I", d[0x40:0x44])[0]
    names = {}
    for v, f, n in parse(os.path.join(FS, "cdi_hotel.stb")):
        if f & CODE_FLAG and exec_off <= v < code_end:
            names.setdefault(v, n)
    starts = sorted(names)
    md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
    md.detail = True

    funcs = {}
    missing = collections.Counter()
    for k, s in enumerate(starts):
        e = starts[k + 1] if k + 1 < len(starts) else code_end
        insns, pc = [], s
        while pc < e:
            got = list(md.disasm(d[pc:e], pc, count=1))
            if not got:
                pc += 2
                continue
            i = got[0]
            insns.append(i)
            pc += i.size + (2 if i.mnemonic == "trap" else 0)  # OS-9 call number word follows trap
        funcs[s] = Func(names[s], s, e, insns, None)

    # Branches/bsr into the middle of another function (shared tails, local subroutines): give each such
    # address its own entry point, a copy of the owning function's code from there to its end.
    def branch_targets(f):
        for i in f.insns:
            base = i.mnemonic.partition(".")[0]
            if base == "bsr" or base == "bra" or (base.startswith("b") and base[1:] in CC):
                yield (i.address + 2 + i.operands[0].br_disp.disp) & 0xFFFFFFFF
            elif base.startswith("db") and base[2:] in CC:
                yield (i.address + 2 + i.operands[1].br_disp.disp) & 0xFFFFFFFF

    changed = True
    while changed:
        changed = False
        for f in list(funcs.values()):
            for t in branch_targets(f):
                if t in funcs or (f.start <= t < f.end and t in f.addrs and f.name == names.get(f.start)):
                    continue
                owner = next((g for g in funcs.values() if g.start <= t < g.end and t in g.addrs
                              and g.name == names.get(g.start)), None)
                if owner is None or (owner is f and f.start <= t < f.end):
                    continue
                funcs[t] = Func(f"{owner.name}_{t:x}", t, owner.end, [i for i in owner.insns if i.address >= t], None)
                names[t] = funcs[t].name
                changed = True
    starts = sorted(funcs)
    for f in funcs.values():
        f.funcs = funcs

    chunks = []
    for s in starts:
        f = funcs[s]
        chunks.append(f.emit(d))
        missing.update(f.missing)
    header = '#include "m68k.h"\n\n' + "\n".join(f"void {cfn(s)}(void);" for s in starts) + "\n\n"
    # split into a few files so the compiler copes
    per = 60
    for n in range(0, len(chunks), per):
        with open(os.path.join(outdir, f"recomp_{n // per:02d}.c"), "w") as o:
            o.write(header + "\n\n".join(chunks[n:n + per]) + "\n")
    with open(os.path.join(outdir, "recomp_table.c"), "w") as o:
        o.write('#include "m68k.h"\n\n' + "\n".join(f"void {cfn(s)}(void);" for s in starts) + "\n\n")
        o.write("const struct { uint32_t addr; RecompFn fn; const char *name; } recomp_table[] = {\n")
        o.write("\n".join(f'    {{ 0x{s:x}, {cfn(s)}, "{names[s]}" }},' for s in starts))
        o.write("\n};\nconst int recomp_count = %d;\n" % len(starts))
    total = sum(len(f.insns) for f in funcs.values())
    bad = sum(missing.values())
    print(f"{len(funcs)} functions, {total} instructions, {bad} not translated ({100 * (total - bad) / total:.2f}% done)")
    for what, n in missing.most_common(15):
        print(f"  {n:5} {what}")


if __name__ == "__main__":
    main()

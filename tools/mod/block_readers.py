#!/usr/bin/env python3
"""Static candidate primitives for top-level INI fields, not proof of a reader.

A tracked value is a pointer loaded from a specified absolute global. The
analysis is deliberately local to a recovered basic block: no claims are made
about register values across CFG joins, calls or unmodelled transformations.
The input is an address/size inventory and the loaded image; no game bytes are
written by this module.
"""
from collections import defaultdict

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_AC_READ
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_INVALID

# Aliases are normalized before killing values: writing AL invalidates EAX.
FAMILIES = {
    name: full for full, aliases in {
        'eax': ('eax', 'ax', 'al', 'ah'), 'ebx': ('ebx', 'bx', 'bl', 'bh'),
        'ecx': ('ecx', 'cx', 'cl', 'ch'), 'edx': ('edx', 'dx', 'dl', 'dh'),
        'esi': ('esi', 'si', 'sil'), 'edi': ('edi', 'di', 'dil'),
        'ebp': ('ebp', 'bp', 'bpl'), 'esp': ('esp', 'sp', 'spl'),
    }.items() for name in aliases
}
VOLATILE = {'eax', 'ecx', 'edx'}


def global_reads(data, base, blocks, globals_by_address):
    """Return (function, global address, offset, instruction) candidate tuples.

    globals_by_address is a set of absolute pointer addresses. A load and the
    field access must occur in the same basic block; register copies propagate.
    Calls kill volatile aliases; arbitrary writes and partial writes kill their
    destination. Memory stores and indexed addresses are not field reads.
    """
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    result = set()
    for function, spans in sorted(blocks.items()):
        for start, size in sorted(spans):
            if size <= 0 or start < base or start + size > base + len(data):
                raise ValueError(f'invalid block at {start:#x}')
            instructions = list(decoder.disasm(data[start-base:start-base+size], start))
            if not instructions or instructions[-1].address + instructions[-1].size != start + size:
                raise ValueError(f'truncated block at {start:#x}')
            tracked = {}
            for ins in instructions:
                for op in ins.operands:
                    if op.type != X86_OP_MEM or not (op.access & CS_AC_READ) or op.mem.index != X86_REG_INVALID:
                        continue
                    reg = FAMILIES.get(ins.reg_name(op.mem.base))
                    if reg in tracked:
                        result.add ( (function, tracked[reg], op.mem.disp, ins.address) )
                copied = None
                if ins.mnemonic == 'mov' and len(ins.operands) == 2:
                    dst, src = ins.operands
                    if dst.type == X86_OP_REG and dst.size == 4:
                        if src.type == X86_OP_MEM and src.size == 4 and src.mem.base == X86_REG_INVALID and src.mem.index == X86_REG_INVALID:
                            absolute = src.mem.disp & 0xffffffff
                            if absolute in globals_by_address:
                                copied = absolute
                        elif src.type == X86_OP_REG and src.size == 4:
                            copied = tracked.get(FAMILIES.get(ins.reg_name(src.reg)))
                _, writes = ins.regs_access()
                for reg in writes:
                    tracked.pop(FAMILIES.get(ins.reg_name(reg)), None)
                if ins.mnemonic == 'call':
                    for reg in VOLATILE:
                        tracked.pop(reg, None)
                if copied is not None:
                    tracked[FAMILIES[ins.reg_name(ins.operands[0].reg)]] = copied
    return sorted(result)


def getter_callers(functions, getters):
    """Map each known getter entry to direct caller entries, including tailcalls.

    The graph is produced by rw_function_map.decode_block_inventory; indirect
    calls cannot be resolved and getter identification belongs to the caller.
    """
    result = defaultdict(set)
    for caller, function in functions.items():
        for target in function.calls & getters:
            result[target].add(caller)
    return {getter: sorted(result[getter]) for getter in sorted(getters)}

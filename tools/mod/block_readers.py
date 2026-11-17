#!/usr/bin/env python3
"""Static candidate primitives for top-level INI fields, not proof of a reader.

A tracked value is a pointer loaded from a specified absolute global. The
analysis propagates possible values over direct CFG edges within each function;
indirect edges, stack spills and unmodelled transformations are not followed.
The input is an address/size inventory and the loaded image; no game bytes are
written by this module.
"""
from collections import defaultdict, deque
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_AC_READ
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_INVALID

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


def global_reads(data, base, blocks, globals_by_address, pointer_paths=(), virtual_getters=()):
    """Return (function, global address, pointer path, offset, instruction).

    Propagate possible pointer values along direct CFG edges. Union at joins is
    a may-analysis, not a guarantee that a candidate is a reachable read.
    Calls kill volatile registers, partial or other writes kill aliases; stack
    spills, indirect branches, indexed addresses and address-only LEAs are not
    followed. Each load and access belongs to the same recovered function.
    """
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    result = set()
    pointer_paths = set(pointer_paths)
    virtual_getters = set(virtual_getters)
    for function, spans in sorted(blocks.items()):
        decoded = {}
        for start, size in sorted(spans):
            if size <= 0 or start < base or start + size > base + len(data):
                raise ValueError(f'invalid block at {start:#x}')
            instructions = list(decoder.disasm(data[start-base:start-base+size], start))
            if not instructions or instructions[-1].address + instructions[-1].size != start + size:
                raise ValueError(f'truncated block at {start:#x}')
            decoded[start] = instructions
        if function not in decoded:
            raise ValueError(f'function has no entry block at {function:#x}')
        states = {function: {}}
        pending = deque([function])
        while pending:
            start = pending.popleft()
            tracked = {reg: set(values) for reg, values in states[start].items()}
            for ins in decoded[start]:
                for op in ins.operands:
                    if ins.mnemonic == 'lea' or op.type != X86_OP_MEM or not (op.access & CS_AC_READ) or op.mem.index != X86_REG_INVALID:
                        continue
                    reg = FAMILIES.get(ins.reg_name(op.mem.base))
                    for pointer, path in tracked.get(reg, ()):
                        if not path or path[-1] != 'vtable':
                            result.add((function, pointer, path, op.mem.disp, ins.address))
                copied = set()
                virtual_returns = set()
                if ins.mnemonic == 'call' and ins.operands and ins.operands[0].type == X86_OP_MEM:
                    mem = ins.operands[0].mem
                    if mem.index == X86_REG_INVALID:
                        tables = tracked.get(FAMILIES.get(ins.reg_name(mem.base)), ())
                        receivers = tracked.get('ecx', ())
                        for pointer, path in receivers:
                            if (pointer, path, mem.disp) in virtual_getters and (pointer, path + ('vtable',)) in tables:
                                virtual_returns.add((pointer, path + (f'v{mem.disp:X}',)))
                if ins.mnemonic == 'mov' and len(ins.operands) == 2:
                    dst, src = ins.operands
                    if dst.type == X86_OP_REG and dst.size == 4:
                        if src.type == X86_OP_MEM and src.size == 4 and src.mem.base == X86_REG_INVALID and src.mem.index == X86_REG_INVALID:
                            absolute = src.mem.disp & 0xffffffff
                            if absolute in globals_by_address:
                                copied.add((absolute, ()))
                        elif src.type == X86_OP_MEM and src.size == 4 and src.mem.index == X86_REG_INVALID:
                            for pointer, path in tracked.get(FAMILIES.get(ins.reg_name(src.mem.base)), ()):
                                nested = path + (src.mem.disp,)
                                if (pointer, nested) in pointer_paths:
                                    copied.add((pointer, nested))
                                if src.mem.disp == 0 and any(p == pointer and pp == path for p, pp, _ in virtual_getters):
                                    copied.add((pointer, path + ('vtable',)))
                        elif src.type == X86_OP_REG and src.size == 4:
                            copied.update(tracked.get(FAMILIES.get(ins.reg_name(src.reg)), ()))
                _, writes = ins.regs_access()
                for reg in writes:
                    tracked.pop(FAMILIES.get(ins.reg_name(reg)), None)
                if ins.mnemonic == 'call':
                    for reg in VOLATILE:
                        tracked.pop(reg, None)
                if virtual_returns:
                    tracked['eax'] = virtual_returns
                if copied:
                    tracked[FAMILIES[ins.reg_name(ins.operands[0].reg)]] = copied
            last = decoded[start][-1]
            successors = set()
            if last.mnemonic.startswith('j'):
                if last.operands and last.operands[0].type == X86_OP_IMM:
                    successors.add(last.operands[0].imm)
                if last.mnemonic != 'jmp':
                    successors.add(last.address + last.size)
            elif not (last.mnemonic.startswith('ret') or last.mnemonic in {'int3', 'ud2', 'hlt'}):
                successors.add(last.address + last.size)
            for target in sorted(successors & decoded.keys()):
                previous = states.get(target)
                merged = {reg: set(values) for reg, values in (previous or {}).items()}
                for reg, values in tracked.items():
                    merged.setdefault(reg, set()).update(values)
                if previous is None or merged != previous:
                    states[target] = merged
                    pending.append(target)
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


def field_table(data, base, address, end_address=None):
    """Read 16-byte INI field entries from a mapped image, through the terminator.

    Values and raw bytes must never be written to the report. A malformed table
    or name is an error rather than a partial result. end_address is an
    explicitly verified exclusive bound for a table without a null sentinel.
    """
    rows = {}
    if end_address is not None and (end_address < address or (end_address - address) % 16):
        raise ValueError('invalid field table bound')
    while True:
        if address == end_address:
            return rows
        pos = address - base
        if pos < 0 or pos + 16 > len(data):
            raise ValueError(f'field table outside image at {address:#x}')
        name_ptr, parser, _, offset = struct.unpack_from('<IIII', data, pos)
        if name_ptr == 0:
            return rows
        name_pos = name_ptr - base
        if not 0 <= name_pos < len(data):
            raise ValueError(f'field name outside image at {address:#x}')
        end = data.find(b'\0', name_pos, min(name_pos + 256, len(data)))
        if end < 0 or not parser:
            raise ValueError(f'invalid field entry at {address:#x}')
        name = data[name_pos:end].decode('ascii')
        if name.lower() in rows:
            raise ValueError(f'duplicate field name {name}')
        rows[name.lower()] = offset
        address += 16


def getter_offsets(data, base, blocks, getters):
    """Read offsets reached from ECX at entry to named thiscall getters.

    Deliberately inspect only each entry basic block: branch joins and indirect
    calls need type/flow information not present in the inventory. An offset
    is only a hint: named BFME2 matches can be wrong, especially tier B.
    """
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    result = defaultdict(set)
    for address in getters:
        spans = blocks.get(address, ())
        entry = next((size for start, size in spans if start == address), None)
        if entry is None:
            continue
        if entry <= 0 or address < base or address + entry > base + len(data):
            raise ValueError(f'invalid getter block at {address:#x}')
        instructions = list(decoder.disasm(data[address-base:address-base+entry], address))
        if not instructions or instructions[-1].address + instructions[-1].size != address + entry:
            raise ValueError(f'truncated getter block at {address:#x}')
        tracked = {'ecx'}
        for ins in instructions:
            for op in ins.operands:
                if (ins.mnemonic != 'lea' and op.type == X86_OP_MEM and op.access & CS_AC_READ
                        and op.mem.index == X86_REG_INVALID and FAMILIES.get(ins.reg_name(op.mem.base)) in tracked):
                    result[address].add(op.mem.disp)
            copied = False
            if ins.mnemonic == 'mov' and len(ins.operands) == 2:
                dst, src = ins.operands
                copied = (dst.type == X86_OP_REG and dst.size == 4 and src.type == X86_OP_REG and src.size == 4
                          and FAMILIES.get(ins.reg_name(src.reg)) in tracked)
            _, writes = ins.regs_access()
            for reg in writes:
                tracked.discard(FAMILIES.get(ins.reg_name(reg)))
            if ins.mnemonic == 'call':
                tracked.difference_update(VOLATILE)
            if copied:
                tracked.add(FAMILIES[ins.reg_name(ins.operands[0].reg)])
    return dict(result)

#!/usr/bin/env python3
"""Building blocks for the RotWK module call-graph audit.

This is a work in progress: these routines operate on a bounded x86 function;
there is not yet a retail inventory, module resolver, report generator or CLI.
No bytes from retail images are emitted.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from pathlib import Path
import re

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM


@dataclass(frozen=True)
class Function:
    calls: frozenset[int]
    vcalls: frozenset[int]
    instructions: int


def decode_function(data: bytes, base: int, start: int, end: int, entries: set[int]) -> Function:
    """Walk control flow in [start, end); only known entries become tail calls.

    Branches into the current function are followed, not classified as calls.
    Indirect memory calls record their displacement only: they are not resolved.
    The caller supplies independently determined function boundaries and entries.
    """
    if not (base <= start < end <= base + len(data)):
        raise ValueError("function bounds outside image")
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    pending = [start]
    visited: set[int] = set()
    calls: set[int] = set()
    vcalls: set[int] = set()
    while pending:
        address = pending.pop()
        while start <= address < end and address not in visited:
            offset = address - base
            ins = next(decoder.disasm(data[offset: min(end - base, offset + 15)], address, count=1), None)
            if ins is None:
                raise ValueError(f"undecodable instruction at 0x{address:X}")
            visited.add(address)
            nxt = address + ins.size
            if ins.mnemonic.startswith("ret") or ins.mnemonic in ("int3", "ud2", "hlt"):
                break
            if ins.mnemonic == "call":
                op = ins.operands[0]
                if op.type == X86_OP_IMM:
                    calls.add(op.imm)
                elif op.type == X86_OP_MEM and op.mem.base:
                    vcalls.add(op.mem.disp)
            elif ins.mnemonic == "jmp":
                op = ins.operands[0]
                if op.type == X86_OP_IMM:
                    if op.imm in entries and op.imm != start:
                        calls.add(op.imm)
                    elif start <= op.imm < end:
                        pending.append(op.imm)
                break
            elif ins.mnemonic.startswith("j") and ins.operands[0].type == X86_OP_IMM:
                target = ins.operands[0].imm
                if start <= target < end:
                    pending.append(target)
            address = nxt
    return Function(frozenset(calls), frozenset(vcalls), len(visited))


def walk_graph(roots: set[int], functions: dict[int, Function], stops: set[int]):
    """Return minimum depths and all incoming edges; stopped nodes are included."""
    depth: dict[int, int] = {}
    incoming: dict[int, set[int]] = {}
    queue = deque((root, 0) for root in sorted(roots))
    while queue:
        address, d = queue.popleft()
        if address in depth:
            continue
        depth[address] = d
        if address in stops or address not in functions:
            continue
        for child in sorted(functions[address].calls):
            incoming.setdefault(child, set()).add(address)
            queue.append((child, d + 1))
    return depth, incoming


CITE = re.compile(r"\bRW\s+0x([0-9a-fA-F]{6,8})\b")
PARTIAL = re.compile(r"\bS-\d+\b|\bNOT PORTED\b|\bINFERENCE\b", re.I)


def scan_citations(root: Path) -> dict[int, str]:
    """Classify explicit cites; any unmarked citation takes precedence."""
    found: dict[int, str] = {}
    for path in sorted(root.rglob('*')):
        if not path.is_file() or path.suffix.lower() not in {'.c', '.cc', '.cpp', '.h', '.hpp', '.inl', '.inc'}:
            continue
        lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
        for i, line in enumerate(lines):
            # A marker on the citation line or the next line qualifies the citation.
            context = line + '\n' + (lines[i + 1] if i + 1 < len(lines) else '')
            status = 'partial' if PARTIAL.search(context) else 'ported'
            for match in CITE.finditer(line):
                address = int(match.group(1), 16)
                if address not in found or status == 'ported':
                    found[address] = status
    return found


def importance(objects: int, depth: int, instructions: int, module: str, address: int):
    return (-objects, depth, -instructions, module, address)


def cited_neighbours(address: int, functions: dict[int, Function], citations: dict[int, str]):
    """Cited immediate callers and callees, sorted by address, with direction.

    This is evidence for manual investigation, not proof that the uncited
    function itself was ported. Only citations supplied by the caller count.
    """
    result: set[tuple[str, int, str]] = set()
    for callee in functions.get(address, Function(frozenset(), frozenset(), 0)).calls:
        if callee in citations:
            result.add(('callee', callee, citations[callee]))
    for caller, function in functions.items():
        if address in function.calls and caller in citations:
            result.add(('caller', caller, citations[caller]))
    return sorted(result)


class DecompLookup:
    """Optional counterpart map using the same bounds and fields as rw2decomp."""

    def __init__(self, path: Path):
        import sys
        from bisect import bisect_right

        self._bisect_right = bisect_right
        self.rows = []
        self.starts = []
        if not path.exists():
            return
        # rw2decomp owns CSV parsing and RotWK RVA/size interpretation.
        re_dir = str(Path(__file__).resolve().parents[1] / 're')
        if re_dir not in sys.path:
            sys.path.insert(0, re_dir)
        import rw2decomp
        self.rows = rw2decomp.load(path)
        self.starts = [r['_rva'] for r in self.rows]

    def get(self, address: int) -> str:
        rva = address - 0x400000
        i = self._bisect_right(self.starts, rva) - 1
        if i < 0 or rva >= self.starts[i] + self.rows[i]['_size']:
            return ''
        row = self.rows[i]
        # No BFME2 counterpart: do not present the RotWK name as decomp.
        if row['tier'] == '-':
            return ''
        name = row.get('decomp_qualname') or row.get('decomp_name') or ''
        return f"{name} (tier {row['tier']})" if name else ''

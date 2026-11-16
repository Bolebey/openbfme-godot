"""Synthetic x86 only: no retail image needed."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import block_readers as br
import rw_function_map as fm


def test_global_copy_overwrite_store_and_partial_register():
    # mov eax,[0x2000]; mov ebx,eax; mov ecx,[ebx+0x3c];
    # mov ebx,7; mov edx,[ebx+0x40]; mov [eax+0x44],ecx;
    # mov al,1; mov edx,[eax+0x48]; ret
    code = bytes.fromhex('a100200000 89c3 8b4b3c bb07000000 8b5340 894844 b001 8b5048 c3')
    reads = br.global_reads(code, 0x1000, {0x1000: ((0x1000, len(code)),)}, {0x2000})
    assert reads == [(0x1000, 0x2000, 0x3c, 0x1007)]


def test_getter_two_callers():
    # Two direct calls to 0x1010; the third function is the getter.
    data = bytes.fromhex('e80b000000c3 e805000000c3 90909090 c3')
    blocks = {0x1000: ((0x1000, 6),), 0x1006: ((0x1006, 6),), 0x1010: ((0x1010, 1),)}
    functions, _ = fm.decode_block_inventory(data, 0x1000, blocks)
    assert br.getter_callers(functions, {0x1010}) == {0x1010: [0x1000, 0x1006]}


def test_volatile_registers_killed_at_calls():
    # Saved EBX survives a call; EAX does not.
    code = bytes.fromhex('a100200000 89c3 e800000000 8b483c 8b4b40 c3')
    assert br.global_reads(code, 0x1000, {0x1000: ((0x1000, len(code)),)}, {0x2000}) == [
        (0x1000, 0x2000, 0x40, 0x100f)]

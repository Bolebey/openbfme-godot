"""Synthetic x86 and source trees: no retail inputs needed."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from rw_function_map import Function, decode_function, walk_graph, scan_citations, importance


def test_cfg_calls_vcalls_and_tail_jumps():
    # 1000: call 1010; call [eax+0x24]; jz 100d; ret; ret.
    # 1010: jmp 1020 (a known tail-call entry); 1020: ret.
    code = bytes.fromhex('e8 0b 00 00 00 ff 50 24 74 03 c3 90 90 c3 90 90 e9 0b 00 00 00') + b'\x90' * 11 + b'\xc3'
    entries = {0x1000, 0x1010, 0x1020}
    first = decode_function(code, 0x1000, 0x1000, 0x1010, entries)
    tail = decode_function(code, 0x1000, 0x1010, 0x1020, entries)
    assert first.calls == {0x1010}
    assert first.vcalls == {0x24}
    assert first.instructions == 5
    assert tail.calls == {0x1020}
    functions = {0x1000: first, 0x1010: tail, 0x1020: Function(frozenset(), frozenset(), 1)}
    depths, incoming = walk_graph({0x1000}, functions, {0x1010})
    assert depths == {0x1000: 0, 0x1010: 1}
    assert incoming == {0x1010: {0x1000}}
    depths, incoming = walk_graph({0x1000}, functions, set())
    assert depths[0x1020] == 2
    assert incoming[0x1020] == {0x1010}


def test_citations_and_importance(tmp_path):
    (tmp_path / 'test.cpp').write_text('// RW 0x401234\n// NOT PORTED\n// RW 0x401235\n\n// RW 0x401236 S-6011\n// RW 0x401236 real\n', encoding='utf-8')
    status = scan_citations(tmp_path)
    assert status == {0x401234: 'partial', 0x401235: 'ported', 0x401236: 'ported'}
    assert status.get(0x401237, 'none') == 'none'
    entries = [(3, 0, 1, 'a', 1), (10, 2, 2, 'b', 2), (10, 1, 2, 'c', 3), (10, 1, 5, 'd', 4)]
    assert [x[3] for x in sorted(entries, key=lambda x: importance(*x))] == ['d', 'c', 'b', 'a']

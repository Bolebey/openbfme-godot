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


def test_neighbours_include_callers_and_callees_only():
    from rw_function_map import cited_neighbours
    functions = {
        1: Function(frozenset({2}), frozenset(), 1),
        2: Function(frozenset({3, 4}), frozenset(), 1),
        4: Function(frozenset(), frozenset(), 1),
    }
    assert cited_neighbours(2, functions, {1: 'partial', 3: 'ported'}) == [
        ('callee', 3, 'ported'), ('caller', 1, 'partial')]
    assert cited_neighbours(4, functions, {}) == []


def test_decomp_lookup_is_optional_and_respects_function_extents(tmp_path):
    from rw_function_map import DecompLookup
    path = tmp_path / 'rw_map.csv'
    assert DecompLookup(path).get(0x401001) == ''
    path.write_text(
        'rotwk_rva,size,bfme2_rva,tier,decomp_qualname,decomp_name\n'
        '0x1000,4,0x2000,A,Thing::run,run\n'
        '0x1010,2,, -,Other::run,run\n'.replace(', -,', ',-,'), encoding='utf-8')
    lookup = DecompLookup(path)
    assert lookup.get(0x401000) == 'Thing::run (tier A)'
    assert lookup.get(0x401003) == 'Thing::run (tier A)'
    assert lookup.get(0x401004) == ''
    assert lookup.get(0x401010) == ''


def test_reports_are_deterministic_sorted_and_show_status_changes(tmp_path, capsys):
    from rw_function_map import MapRow, write_reports
    tsv, md = tmp_path / 'map.tsv', tmp_path / 'map.md'
    rows = [
        MapRow('Low', 0, 0x401000, 'none', objects=1, instructions=20),
        MapRow('High', 1, 0x401010, 'partial', (0x401000,), (0x24,),
               (('caller', 0x401000, 'ported'),), 'Foo::bar (tier A)', 10, 4),
        MapRow('High', 0, 0x401020, 'none', objects=10, instructions=2),
    ]
    assert write_reports(rows, tsv, md, 'example', {0x401100: 'allocator'}, {0x401200: 201}, 200) == []
    text = md.read_text(encoding='utf-8')
    assert text.index('0x00401020') < text.index('0x00401010') < text.index('0x00401000')
    assert '0x00401100: allocator' in text and '201 call sites' in text
    content = tsv.read_text(encoding='utf-8')
    assert '\tdecomp\n' in content and 'vcall +0x24' in content and 'Foo::bar (tier A)' in content
    assert write_reports(list(reversed(rows)), tsv, md, 'example', {0x401100: 'allocator'}, {0x401200: 201}, 200) == []
    assert content == tsv.read_text(encoding='utf-8')
    from dataclasses import replace
    changed = [replace(r, status='ported') if r.function == 0x401010 else r for r in rows]
    assert write_reports(changed, tsv, md, 'example', {}, {}, 200) == [('High', 0x401010, 'partial')]
    assert 'partial -> ported' in capsys.readouterr().out


def test_reports_reject_ambiguous_rows_and_existing_format(tmp_path):
    import pytest
    from rw_function_map import MapRow, write_reports
    tsv, md = tmp_path / 'map.tsv', tmp_path / 'map.md'
    row = MapRow('X', 0, 0x401000, 'none')
    with pytest.raises(ValueError, match='duplicate'):
        write_reports([row, row], tsv, md, 'example', {}, {}, 200)
    tsv.write_text('unexpected\theader\n', encoding='utf-8')
    with pytest.raises(ValueError, match='columns'):
        write_reports([row], tsv, md, 'example', {}, {}, 200)

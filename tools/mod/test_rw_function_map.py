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


def test_module_tables_use_constructor_not_module_data():
    import pytest
    from rw_function_map import discover_module_tables
    image = object()
    records = [{'name': 'Thing', 'create': '0x1000', 'vtable': '0x9000'}]
    def constructor_finder(img, address):
        assert img is image and address == 0x1000
        return 0x1100
    def table_finder(img, address):
        assert img is image and address == 0x1100
        return {0x10: 0x3100, 0: 0x3000}
    result = discover_module_tables(image, records, constructor_finder, table_finder)
    assert result[0].data_vtable == 0x9000
    assert result[0].tables == ((0, 0x3000), (0x10, 0x3100))
    with pytest.raises(ValueError, match='constructor not found'):
        discover_module_tables(image, records, lambda *_: None, table_finder)
    with pytest.raises(ValueError, match='primary vtable not found'):
        discover_module_tables(image, records, constructor_finder, lambda *_: {0x10: 0x3100})
    with pytest.raises(ValueError, match='duplicate module'):
        discover_module_tables(image, records * 2, constructor_finder, table_finder)


def test_replay_module_inheritance_and_replacements():
    import pytest
    from rw_function_map import ModuleEntry, add_module, copy_module_state, remove_module
    make = lambda n, t, **kw: ModuleEntry(n, t, 'behavior', kw.get('mask', 1), kw.get('ai', False), kw.get('slot7', False), kw.get('inheritable', False))
    parent = (make('A', 'custom_tag'), make('OldAI', 'ai', ai=True), make('OldBody', 'body', slot7=True),
              make('Keep', 'keep', mask=2, inheritable=True))
    copied = copy_module_state(parent)
    # RW 0x73F49B: new AI removes the parent's AI even with a different tag.
    child = add_module(copied, make('NewAI', 'new_ai', ai=True), child=True)
    assert {x.name for x in child} == {'A', 'NewAI', 'OldBody', 'Keep'}
    # RW 0x73F4B4: slot 7 clears only in child mode.
    child = add_module(child, make('NewBody', 'new_body', slot7=True), child=True)
    assert {x.name for x in child} == {'A', 'NewAI', 'NewBody', 'Keep'}
    child = add_module(child, make('B', 'custom_tag'), child=True)
    assert {x.name for x in child} == {'B', 'NewAI', 'NewBody', 'Keep'}
    child, old = remove_module(child, 'custom_tag')
    child = add_module(child, make('B', 'new_tag'), child=True, replacing=old)
    assert any(x.tag == 'new_tag' for x in child)
    with pytest.raises(ValueError, match='same class'):
        add_module(child, make('Wrong', 'other'), child=True, replacing=old)
    with pytest.raises(ValueError, match='expected one'):
        remove_module(child, 'missing')
    # RW 0x73F33D: non-child mask clears copied entries, except inheritable.
    plain = add_module(copied, make('C', 'new', mask=1), child=False)
    assert {x.name for x in plain} == {'Keep', 'C'}
    draw = ModuleEntry('DrawA', 'draw', 'draw', 4, False, False)
    with pytest.raises(ValueError, match='duplicate'):
        add_module((draw,), ModuleEntry('DrawB', 'draw', 'draw', 4, False, False), child=True)


def test_parse_module_events_uses_any_tag_and_skips_nested_blocks():
    import pytest
    from types import SimpleNamespace
    from rw_function_map import parse_module_events
    grammar = SimpleNamespace(classes={'A': {}, 'B': {}}, object={'armorset': ('block', {'armor': ('line', None)}), 'behavior': ('line', None)})
    text = [
        'ArmorSet', 'Armor = X', 'End',
        'Behavior = A CustomTag', 'SomeField = 1', 'End',
        'RemoveModule CustomTag',
        'ReplaceModule OldTag', 'Behavior = B NewTag', 'End', 'End',
        'InheritableModule', 'Draw = A DrawTag', 'End', 'End', 'End',
    ]
    lines = [(line, str(i)) for i, line in enumerate(text)]
    events, end = parse_module_events(lines, 0, grammar)
    assert end == len(lines)
    assert [(x.action, x.name, x.tag, x.inheritable) for x in events] == [
        ('add', 'A', 'CustomTag', False), ('remove', '', 'CustomTag', False),
        ('replace_begin', '', 'OldTag', False), ('add', 'B', 'NewTag', False),
        ('replace_end', '', 'OldTag', False), ('add', 'A', 'DrawTag', True)]
    with pytest.raises(ValueError, match='unknown module class'):
        parse_module_events([('Behavior = Unknown tag', '1'), ('End', '2')], 0, grammar)
    with pytest.raises(ValueError, match='not closed'):
        parse_module_events([('foo = bar', '1')], 0, grammar)


def test_module_field_that_can_be_one_line_does_not_eat_object_end():
    from types import SimpleNamespace
    from rw_function_map import parse_module_events
    grammar = SimpleNamespace(
        classes={'A': {'addemotion': ('block', {'subfield': ('line', None)})}},
        object={'geometry': ('line', None)})
    text = ['Behavior = A Arbitrary', 'AddEmotion = value', 'AddEmotion = OVERRIDE other', 'End', 'AddEmotion = last', 'End', 'Geometry = X', 'End']
    events, end = parse_module_events([(line, str(i)) for i, line in enumerate(text)], 0, grammar)
    assert end == len(text)
    assert [(e.name, e.tag) for e in events] == [('A', 'Arbitrary')]


def test_replay_extracted_events_uses_registry_and_replacement():
    import pytest
    from rw_function_map import ModuleEntry, ModuleEvent, replay_module_events
    registry = [dict(name='A', type=0, mask=1, isAiModuleData=False, slot7=False),
                dict(name='AI', type=0, mask=17, isAiModuleData=True, slot7=False)]
    initial = (ModuleEntry('A', 'old', 'behavior', 1, False, False),)
    events = [ModuleEvent('replace_begin', tag='old', source='2'),
              ModuleEvent('add', tag='new', name='A', category='behavior', source='3'),
              ModuleEvent('replace_end', tag='old', source='4')]
    result = replay_module_events(initial, events, registry, child=True)
    assert [(x.name, x.tag) for x in result] == [('A', 'new')]
    with pytest.raises(ValueError, match='3: ReplaceModule'):
        replay_module_events(initial, [events[0], ModuleEvent('add', tag='new', name='AI', category='behavior', source='3')], registry, child=True)
    with pytest.raises(ValueError, match='unclosed replacement'):
        replay_module_events(initial, events[:2], registry, child=True)


def test_object_boundary_mismatch_is_not_silently_accepted():
    import pytest
    from types import SimpleNamespace
    from rw_function_map import parse_module_events
    grammar = SimpleNamespace(classes={}, object={})
    lines = [('End', '1'), ('Other = ignored', '2')]
    with pytest.raises(ValueError, match='boundary mismatch'):
        parse_module_events(lines, 0, grammar, limit=2)

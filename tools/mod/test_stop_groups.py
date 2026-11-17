"""Synthetic examples; no game or third-party disassembler is needed."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from stop_groups import Inventory, cluster, enrich, read_stops, rw_addresses


def row(n, gap='', kind='code'):
    return f'| S-{n} | {kind} | area | {gap} | PORT-1 | — | — | — |\n'


def test_function_reference_unplaced_closed(tmp_path):
    p = tmp_path/'stops.md'
    p.write_text(row(325, 'RW 0x401002') + row(326, 'RW 0x401008') + row(327, 'waits for S-325') + row(328) + row(329, 'RW 0x401003', 'closed'))
    stops = read_stops([p])
    inv = Inventory([{'address': 0x401000, 'blocks': [[0x401000, 32]]}])
    enrich(stops, {}, set(), inv)
    groups, unplaced = cluster(stops, {})
    assert sorted(groups) == [['S-325', 'S-326', 'S-327']]
    assert unplaced == ['S-328']
    assert 'S-329' not in stops


def test_split_large_component_by_function(tmp_path):
    p = tmp_path/'stops.md'
    p.write_text(''.join(row(n, f'ModuleX RW 0x{0x401002 if n < 45 else 0x402002:X}') for n in range(80)))
    stops = read_stops([p])
    inv = Inventory([{'address': 0x401000, 'blocks': [[0x401000, 32]]}, {'address': 0x402000, 'blocks': [[0x402000, 32]]}])
    enrich(stops, {}, {'ModuleX'}, inv)
    groups, unplaced = cluster(stops, {}, limit=40)
    assert max(map(len, groups)) <= 40
    assert sum(map(len, groups)) == 80
    assert all(len(set().union(*(stops[s].functions for s in group))) == 1 for group in groups)
    assert not unplaced


def test_only_rw_citations():
    assert rw_addresses('BFME2 0x401000 RW 0x402000 / 0x402010 and RVA 0x403000') == {0x402000, 0x402010}


def test_known_map_start_beats_false_interior_seed():
    from stop_groups import BinaryResolver
    resolver = BinaryResolver.__new__(BinaryResolver)
    resolver.lo, resolver.hi = 0x401000, 0x403000
    resolver.map_starts = [0x401000]
    resolver.seeds = [0x401000, 0x401100]
    resolver.cache = {}
    resolver.reaches = lambda start, address: start in (0x401000, 0x401100)
    assert resolver.owner(0x401108) == 0x401000

"""Stdlib-only integration checks used by the PORT-268 doctest lane."""
import tempfile
import unittest
from pathlib import Path
from stop_groups import Inventory, cluster, enrich, read_stops


def row(n, text='', kind='code'):
    return f'| S-{n} | {kind} | area | {text} | PORT-1 | — | — | — |\n'


class Grouping(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'stops.md'
            p.write_text(text, encoding='utf-8')
            return read_stops([p])

    def test_same_function_and_reference(self):
        stops = self.parse(row(325, 'RW 0x401002') + row(326, 'RW 0x401008') + row(327, 'waits for S-325'))
        inv = Inventory([{'address': 0x401000, 'blocks': [[0x401000, 32]]}])
        enrich(stops, {}, set(), inv)
        groups, unplaced = cluster(stops, {})
        self.assertEqual(groups, [['S-325', 'S-326', 'S-327']])
        self.assertEqual(unplaced, [])

    def test_large_component_splits_by_function(self):
        text = ''.join(row(n, f'ModuleX RW 0x{0x401002 if n < 45 else 0x402002:X}') for n in range(80))
        stops = self.parse(text)
        inv = Inventory([{'address': 0x401000, 'blocks': [[0x401000, 32]]},
                         {'address': 0x402000, 'blocks': [[0x402000, 32]]}])
        enrich(stops, {}, {'ModuleX'}, inv)
        groups, unplaced = cluster(stops, {}, limit=40)
        self.assertEqual(sum(map(len, groups)), 80)
        self.assertTrue(all(len(group) <= 40 for group in groups))
        self.assertTrue(all(len(set().union(*(stops[s].functions for s in group))) == 1 for group in groups))
        self.assertEqual(unplaced, [])

    def test_unplaced_and_closed(self):
        stops = self.parse(row(328) + row(329, 'RW 0x401002', 'closed'))
        enrich(stops, {}, set(), Inventory([]))
        groups, unplaced = cluster(stops, {})
        self.assertEqual(groups, [])
        self.assertEqual(unplaced, ['S-328'])
        self.assertNotIn('S-329', stops)


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Explain the DISABLED matrix rows from an extracted INI tree or INI zip.

Uses ennorath_graph's include/define/inheritance handling. The generated TSV
contains game/mod data: keep it outside Git (for example in workspace/).
Conditions are data references, not proof that a sweep fixture satisfies them.
This is an initial inventory: model-condition setters are only partially resolved
and nested prerequisite blocks are not resolved; none is not a complete verdict.
"""
from __future__ import annotations

import argparse
import csv
import sys
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

import ennorath_graph as graph

ROOT = Path(__file__).resolve().parents[2]
COLUMNS = ('faction', 'object', 'button', 'condition', 'value', 'satisfied by', 'note')
GATE_OPTIONS = {'NEED_UPGRADE', 'NEED_SPECIAL_POWER_SCIENCE', 'NEEDS_CASTLE_KINDOF',
                'NOT_QUEUEABLE', 'SINGLE_USE_COMMAND', 'HIDE_WHILE_DISABLED',
                'MOUNTED_ONLY', 'UNMOUNTED_ONLY', 'ON_GROUND_ONLY'}
BUTTON_FIELDS = {
    'neededupgrade': 'upgrade', 'science': 'science', 'requiredobject': 'object',
    'requiredobjectfilter': 'object', 'enableonmodelcondition': 'model',
    'disableonmodelcondition': 'model', 'requirelevel': 'level',
}
UPGRADE_FIELDS = {'requiredobjectfilter': 'object', 'prerequisites': 'object',
                  'buildcost': 'other'}
POWER_FIELDS = {'requiredsciences': 'science', 'preventactivationconditions': 'other'}


class IniVfs(graph.Vfs):
    """Read the effective mounted INI snapshot without extracting or committing it."""
    def __init__(self, source: Path):
        super().__init__()
        self.archive = None
        if source.is_dir():
            for p in source.rglob('*'):
                if p.is_file():
                    name = p.relative_to(source).as_posix().lower()
                    if name.startswith('data/ini/') and name.endswith(('.ini', '.inc', '.str')):
                        self.files[name] = (p, 0, p.stat().st_size, 'snapshot')
        elif zipfile.is_zipfile(source):
            self.archive = zipfile.ZipFile(source)
            self.members = {}
            for info in self.archive.infolist():
                name = info.filename.replace('\\', '/').lower()
                if name.startswith('data/ini/') and name.endswith(('.ini', '.inc', '.str')):
                    if name in self.members:
                        raise ValueError(f'duplicate case-insensitive archive path: {name}')
                    self.members[name] = info.filename
                    self.files[name] = (source, 0, info.file_size, 'snapshot')
        else:
            raise ValueError(f'not a directory or zip: {source}')
        if not self.files:
            raise ValueError('snapshot has no data/ini files')

    def read(self, name):
        key = name.lower().replace('\\', '/')
        if self.archive is not None:
            return self.archive.read(self.members[key]).decode('latin1') if key in self.members else None
        return super().read(key)

    def close(self):
        if self.archive is not None:
            self.archive.close()


def load(source: Path):
    vfs = IniVfs(source)
    graph.MISSING.clear()
    try:
        db = graph.load_db(vfs)
        if graph.MISSING:
            raise ValueError('missing includes: ' + ', '.join(sorted(set(graph.MISSING))))
        return db
    finally:
        vfs.close()


class Explainer:
    def __init__(self, db):
        self.db = db
        self.names = {(typ, name.lower()): name for typ, name in db.defs}
        self.upgrade_buttons = defaultdict(set)
        self.science_buttons = defaultdict(set)
        self.button_sets = defaultdict(set)
        self.set_objects = defaultdict(set)
        for typ, name in db.defs:
            fields = self.fields(typ, name)
            if typ == 'commandbutton':
                commands = {v.upper() for k, v in fields if k.lower() == 'command'}
                # A NeededUpgrade is a prerequisite, not a producer.
                for k, v in fields:
                    if k.lower() == 'upgrade' and commands & {'PLAYER_UPGRADE', 'OBJECT_UPGRADE', 'CASTLE_UPGRADE'}:
                        for token in graph.tokens_of(v):
                            self.upgrade_buttons[token.lower()].add(name)
                    if k.lower() == 'science' and 'PURCHASE_SCIENCE' in commands:
                        for token in graph.tokens_of(v):
                            self.science_buttons[token.lower()].add(name)
            elif typ == 'commandset':
                for k, v in fields:
                    if k.isdigit():
                        for token in graph.tokens_of(v):
                            self.button_sets[token.lower()].add(name)
            elif typ == 'object':
                for k, v in fields:
                    if k.lower() == 'commandset':
                        for token in graph.tokens_of(v):
                            self.set_objects[token.lower()].add(name)

    def fields(self, typ, name):
        actual = self.names.get((typ, name.lower()))
        if actual is None:
            return []
        return [(k, v) for k, v, _src, module in graph.effective_fields(self.db, typ, actual) if not module]

    def producers(self, typ, value):
        tokens = list(graph.tokens_of(value))
        if typ == 'upgrade':
            producers = set()
            for token in tokens:
                for button in self.upgrade_buttons.get(token.lower(), ()):
                    for cmdset in self.button_sets.get(button.lower(), ()):
                        producers.update(self.set_objects.get(cmdset.lower(), ()))
            return ', '.join(sorted(producers)) or '-'
        if typ == 'science':
            buttons = set()
            for token in tokens:
                buttons.update(self.science_buttons.get(token.lower(), ()))
            return ', '.join(sorted(buttons)) or '-'
        if typ == 'object':
            objects = {self.names[('object', t.lower())] for t in tokens if ('object', t.lower()) in self.names}
            return ', '.join(sorted(objects)) or '-'
        return '-'

    def model_producers(self, object_name, value):
        """Only identify explicit setters on this object; animation states are readers."""
        name = self.names.get(('object', object_name.lower()))
        if not name:
            return '-'
        fields = graph.effective_fields(self.db, 'object', name)
        wanted = {token.upper() for token in graph.tokens_of(value)}
        setters = set()
        for key, val, _src, tag in fields:
            if tag and key.lower() == 'setmodelcondition':
                # SpecialPowerModule::trigger RW 0x897987: this field sets the
                # condition only when its duration is positive and it triggers.
                if wanted.intersection(t.upper() for t in graph.tokens_of(val)):
                    setters.add(object_name + ':' + tag)
            elif tag and key.lower() == 'behavior' and 'MOUNTED' in wanted:
                if val.split() and val.split()[0].lower() == 'togglemountedspecialabilityupdate':
                    setters.add(object_name + ':' + tag + ' (mount replacement)')
        if 'WEAPONSET_TOGGLE_1' in wanted:
            # RW 0x77B529 / WeaponSetToggle::registerHandlers: this command
            # toggles weapon-set flag 1, mirrored to the model condition.
            sets = {token.lower() for key, val, _src, tag in fields
                    if not tag and key.lower() == 'commandset' for token in graph.tokens_of(val)}
            for (typ, button), _definition in self.db.defs.items():
                if typ != 'commandbutton' or not (self.button_sets.get(button.lower(), set())
                                                   and any(s.lower() in sets for s in self.button_sets[button.lower()])):
                    continue
                if any(k.lower() == 'command' and v.upper() == 'TOGGLE_WEAPONSET' for k, v in self.fields(typ, button)):
                    setters.add(button + ' (weapon-set toggle)')
        return ', '.join(sorted(setters)) or '-'

    def explain(self, row):
        button = row['button']
        if ('commandbutton', button.lower()) not in self.names:
            raise ValueError(f'unknown matrix button: {button}')
        fields = self.fields('commandbutton', button)
        result = []
        def emit(kind, value, note):
            result.append({**{k: row[k] for k in ('faction', 'object', 'button')},
                           'condition': kind, 'value': value,
                           'satisfied by': self.model_producers(row['object'], value) if kind == 'model' else self.producers(kind, value), 'note': note})
        upgrades, powers = [], []
        for key, value in fields:
            low = key.lower()
            if low in BUTTON_FIELDS:
                note = 'CommandButton.' + key
                if BUTTON_FIELDS[low] == 'model':
                    note += ' (candidate setters; activation and duration not verified)'
                emit(BUTTON_FIELDS[low], value, note)
                if low == 'neededupgrade':
                    upgrades.extend(graph.tokens_of(value))
            elif low == 'options':
                for opt in value.split():
                    if opt.upper() in GATE_OPTIONS:
                        emit('option', opt, 'CommandButton.Options')
            elif low == 'upgrade':
                upgrades.extend(graph.tokens_of(value))
            elif low == 'specialpower':
                powers.extend(graph.tokens_of(value))
            elif low == 'neededupgradeany':
                emit('other', value, 'CommandButton.' + key)
        for typ, refs, allowed in (('upgrade', upgrades, UPGRADE_FIELDS), ('specialpower', powers, POWER_FIELDS)):
            for ref in dict.fromkeys(refs):
                if (typ, ref.lower()) not in self.names:
                    raise ValueError(f'unknown {typ}: {ref} in {button}')
                for key, value in self.fields(typ, ref):
                    if key.lower() in allowed:
                        emit(allowed[key.lower()], value, typ + '.' + key + ' (' + ref + ')')
        if not result:
            emit('none', '-', 'No recognized gating field; runtime and fixture checks still apply')
        return result


def read_matrix(path):
    with path.open(encoding='utf-8', newline='') as f:
        reader = csv.DictReader((line for line in f if not line.startswith('#')), delimiter='\t')
        if not reader.fieldnames or not {'faction', 'object', 'button', 'result'} <= set(reader.fieldnames):
            raise ValueError('matrix is missing required columns')
        return [r for r in reader if r['result'] == 'DISABLED']


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ini', type=Path, required=True, help='mounted INI snapshot directory or zip')
    ap.add_argument('--matrix', type=Path, default=ROOT / 'docs/ennorath/MATRIX.tsv')
    ap.add_argument('--out', type=Path, required=True, help='TSV output (keep mod-derived values out of Git)')
    args = ap.parse_args(argv)
    rows = read_matrix(args.matrix)
    explainer = Explainer(load(args.ini))
    result = [item for row in rows for item in explainer.explain(row)]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=COLUMNS, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(result)
    counts = Counter(r['condition'] for r in result)
    print(f'{len(rows)} disabled matrix rows; {len(result)} condition rows; ' + ', '.join(f'{k}={counts[k]}' for k in sorted(counts)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, zipfile.BadZipFile) as e:
        sys.exit(str(e))

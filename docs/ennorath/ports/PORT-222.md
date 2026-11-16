# Port 222 — experience given by an ability does not land (fork issue 532, dashboard task 197)

Status: engine fix implemented in `LevelGrantSpecialPower::grant`; the narrow `PORT-222*` tests are
green in the sandbox; the owner's in-game check and the night run are pending.

## Sources

- RotWK 2.01 `game.dat`, md5 `daed3668006cd90f01c34e5a7da1901f`, read with `tools/mod/rw.py dis` (caveat S-001).
- Ennorath INI 1.9.8.3, read only (no values copied): `data/ini/object/goodfaction/units/imladris/imladriscirdan.ini`
  (`ModuleTag_KingsFavor`, `ModuleTag_KingsFavorAI`), `data/ini/specialpower.ini` (`SpecialAbilityKingsFavorCirdan`),
  `data/ini/includes/commandbutton.inc` (`Command_SpecialAbilityKirdanKingsFavor`), `data/ini/includes/commandset.inc`
  (`CirdanCommandSet` slot 5), `data/ini/includes/experiencelevels_imladris.inc` (`LindonTroopsLevel1..5`),
  `data/ini/includes/_gamedata.inc` (`EXPERIENCE_REQUIRED_GOOD_REGULAR_TROOP_1`).
- Owner's report: the fourth ability of Cirdan (the experience fill) gives nothing; the fix is the mechanism, not that button.

## The button

`Command_SpecialAbilityKirdanKingsFavor` is `SPECIAL_POWER`, `NEED_TARGET_POS` / `CONTEXTMODE_COMMAND`, radius cursor
`KingsFavorRadiusCursor`, in `CirdanCommandSet` slot 5 — the same button `docs/ennorath/MATRIX.tsv` lists as `NO-FIRE`
(the refusal comes from `ActionManager::canDoSpecialPowerAtLocation`, a separate lane: it never gets as far as the grant).
So the matrix row and the owner's report are the same button, and the matrix refusal is not this fix.
The power drives `LevelGrantSpecialPower` with `Experience = 100`, `RadiusEffect = 120`,
`AcceptanceFilter = ANY +LindonPikemanModHorde +LindonPikemanModHordePorcupine +LindonWatcherHorde ALLIES`.

## What RotWK does

- RW 0x8C6FA4 `LevelGrantSpecialPower::triggerAbilityEffect`: the base trigger first (RW 0x8C6FB6 -> RW 0x853EDF), then the
  objects the filter allows inside `RadiusEffect` (+0xD4) around the module's location (+0x44) are sorted near to far
  (RW 0x8C7069 `push 1`, RW 0x8C707D -> RW 0xA39340) and each is handed to RW 0x8C6F32.
- RW 0x8C6F32: the hit's horde (the object of the hit's +0x78 id whose template has KindOf HORDE, RW 0x8C6F57
  `test dword ptr [edx+0x114], 0x2000`) replaces the hit and is granted once per trigger (the granted-id list at
  0x8C6F74..0x8C6F90); anything else every time. Then RW 0x8C6E82.
- RW 0x8C6E82 (the grant): `Experience` at data +0xD0, `jle` out. HORDE (RW 0x8C6EA2 `test byte ptr [ecx+0x115], 0x20`):
  the object's contain (RW 0x694BF8 -> RW 0x68C866, Object +0x258, vslot 0x7C), its member count (vslot 0x180, RW 0x8C6EBD,
  0 -> nothing), `amount = Experience / count` (RW 0x8C6EC9 `fild`/`fild`/`fdivp`), and contain vslot 0x110 iterates the
  members with the context `{ amount, LevelFX (data +0xDC), source = the contain }` (RW 0x8C6EFC..0x8C6F01). Not a HORDE:
  the same routine with a null source and `cvtsi2ss Experience` (RW 0x8C6F09..0x8C6F24).
- RW 0x8C6D43 (the per-object grant): with a source, its vslot 0xC0(member, amount) (RW 0x8C6D4F..0x8C6D5E
  `mov ecx,[esi+8]; call [eax+0xC0]`); without one, the object's tracker (Object +0x26C) when it still accepts
  experience (RW 0x79D322) -> `addExperiencePoints(amount, 1, 1, 1, 0)` (RW 0x79D833). Either way the counter
  (module +0x24) grows and LevelFX lands **on that object** (RW 0x8C6D94..0x8C6DA2 -> RW 0x4B1B5A).
- RW 0x873A02 `HordeContain::addExperience` (already ported, `GameLogic/Object/Contain/HordeContainExperience.cpp`): one
  pool per member template id, a new pool at `amount + 1.0` (RW 0x873A49), every member of that template set to the pool
  (RW 0x86BC09 -> RW 0x79D8EF), the horde itself following a higher pool, a rank gain running the banner check
  (RW 0x873ACF -> RW 0x8719E4). Already ported before this lane; the grant simply did not use it.

## What the port did

`LevelGrantSpecialPower::grant` split `Experience` by the member count and called `addExperiencePoints` on each member's
own tracker. For a six-member Lindon horde that is 100 / 6 = 16.67 per member, below `LindonTroopsLevel2`
(`RequiredExperience` 100), so the bar never moved and the horde never levelled — the owner's symptom. The horde pool
(`HordeContain::addExperience`) existed but no experience-granting module reached it through this path.

## The fix

`grant` now follows RW 0x8C6E82/0x8C6D43 as read above: the horde branch takes the member count and the split amount and
feeds `HordeContainInterface::addExperience` per member; a non-horde keeps the whole amount on its own tracker; the counter
and the LevelFX moved inside the per-object grant, so a horde emits one per member, as retail does. No second path: the
ability update (RW 0x853EDF / 0x8C6FA4), the horde pool and `HordeContain` are unchanged; no new simulation state,
`crc`/`xfer` untouched (only the value of `m_granted`, which was already hashed).

Note on the shared mechanism: `SpecialAbilityUpdate::triggerAbilityEffect` RW 0x853EDF still puts `AwardXPForTriggering` on
the casting object's tracker directly (RW 0x79D833), not through the contain vslot 0xC0 of RW 0x8C6D43. That is the same
class of reader but a different routine and a different module data field, so it stays out of this lane (see the PR's
"Осталось").

## Tests

`engine/tests/test_port_222.cpp`, lane `engine/cmake/Port222.cmake`, filter `-tc="PORT-222*"`. Synthetic INI only
(no game or mod files), the mod's numbers: two levels 1 / 100 on the port's own horde template and `Experience = 100`,
`RadiusEffect = 120`.

- `PORT-222 King's Favor fills a horde's experience through the pool`: one cast on a six-member horde at its own ground
  point; the horde's experience goes to `before + 100` (six shares plus the pool's `+1.0` start), its rank to 2, all six
  members sit on the pool value, and `granted()` is 6.
- `PORT-222 a level grant on a lone unit adds the whole amount to its tracker`: the same module data on one non-horde
  object; the whole 100 lands, rank 2, `granted()` 1. This one holds the neighbouring behaviour.

Sandbox run with the fix (`engine/build/openbfme_tests -tc="PORT-222*"`):

```
[doctest] test cases:  2 |  2 passed | 0 failed | 6694 skipped
[doctest] assertions: 60 | 60 passed | 0 failed |
```

Without the fix (the previous `grant` restored, tests unchanged) the first case is red: the horde stays at its starting
experience and rank 1 (`granted()` 0), because the 100 / 6 shares never reach the pool; the second case still passes.

## Affected Ennorath objects

Every template whose power carries `LevelGrantSpecialPower` and is cast on a horde, Cirdan's King's Favor being the one the
owner reported. Hordes that receive experience from kills, production or other abilities are untouched (their paths
already went through `HordeContain::addExperience` or the tracker).

## Left over

- `AwardXPForTriggering` (RW 0x853EDF) still lands on the caster's own tracker, not through the contain interface
  (RW 0x79D833): a hero in a horde is awarded on its own tracker. Not caused by this button and not the reported symptom;
  it is the same class of reader in `SpecialAbilityUpdate`, a separate step.
- The `NO-FIRE` row of `docs/ennorath/MATRIX.tsv` is the targeting refusal (`ActionManager::canDoSpecialPowerAtLocation`),
  not the grant; it stays as it is.

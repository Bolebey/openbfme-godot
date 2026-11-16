# PORT-220: availability investigation (in progress)

Image: RotWK 2.01, MD5 `daed3668006cd90f01c34e5a7da1901f`. Instructions
read with `tools/mod/rw.py`; DONOR BFME2 0x0053BD66 is
`Code/GameEngine/Source/GameClient/GUI/ControlBar/ControlBarRappellerCount.cpp`.
The following is a partial investigation, **not** a complete comparison of the
function. See `docs/re/rotwk/RW_0x942733.md` for the existing arm inventory.

## Matrix census

The checked-in matrix has 587 DISABLED rows for 177 distinct buttons. Resolving
those buttons in the Ennorath 1.9.8.3 INI snapshot with `ennorath_graph.load_db`
finds all of them: 423 object-upgrade, 138 special-power, 19 horde-formation,
and 7 fire-weapon rows. Counts are rows, not distinct buttons. Of the 423
object-upgrade rows, 391 have a NeededUpgrade field. These counts do not establish
why any particular button was disabled: upgrade ownership, object state, other
fields and the actual runtime evaluator still need to be checked.

## Common gates checked so far

| RotWK addresses | Decision | Current port comparison |
|---|---|---|
| 0x942733–0x942798 | Set progress to one, resolve local player and substitute a command center for command types 0x20 and 0x26; hide without player or object. | `evaluate` receives an object and uses its controlling player; center substitution and progress require a caller-level comparison. Not yet classified. |
| 0x9427A0–0x9427B7 | Script-status bits 0/1 or disabled mask bit 5 hide. | Same object gates in `evaluate`; player selection still needs comparison. |
| 0x9427BD–0x9427E5 | Mounted-only / unmounted-only mismatches restrict. | Same in `evaluate`. |
| 0x9427EB–0x94280D | Option 0x40000000 asks AI virtual slot 0x228; nonzero restricts. | Missing in `evaluate`; impact on matrix not yet established. |
| 0x94280D–0x942826 | Except command 0x18, model-condition helper 0x942490 returning hidden causes restricted. | Same structure in `evaluate`; helper and command identity still need full comparison. |
| 0x942826–0x94282D | Nonzero object byte 0x45B restricts. | No corresponding check visible in `evaluate`; byte's semantics and matrix impact unresolved. |
| 0x942833–0x94284E | Missing experience tracker or positive required rank above current rank hides. | Same in `evaluate`. |
| 0x942854–0x9428C6 | Disabled check, sole bit-8 exception, six command exceptions and recursive hidden-vs-restricted test. | Same structure in `evaluate`; bit and enum mappings require further verification. |
| 0x9428CB–0x942993 | NEED_UPGRADE: player upgrades query 0x6AC2AF; object upgrades query 0x691421; ANY short-circuits on an owned upgrade, ALL restricts on a missing entry. Missing object upgrade also checks buildability; null entries are skipped. | Same structure in `evaluate` for player and object entries; unknown upgrade-type handling and selected player need further comparison. |
| 0x942993–0x942A24 | NEEDS_CASTLE_KINDOF tests castle members, hides if no match. | Same structure in `evaluate`; detailed prior analysis: `PORT130_RW_ANALYSIS.md`. |
| 0x942A24–0x942A58 | Production slot 0x54 and NOT_QUEUEABLE restrict; slot 0x44 equal to 20 marks queue full. | NOT_QUEUEABLE matches; queue handling must be checked separately per switch arm. |

DONOR BFME2 0x0053BD66 (`ControlBarRappellerCount.cpp:405`) shows the same
broad common-gate sequence. The RotWK instructions above, not donor names,
are the authority for these classifications.

## Outstanding

Read all switch arms and their call sites, distinguish fixture restrictions from
port differences using the actual gating fields, and test every changed branch.
PR 518 is open and changes the construction arms of `ControlBar::evaluate`;
its diff was inspected, but no overlapping construction change is made here.
No claim of a complete port or of a test pass is made by this note.

# Port 462: unit voice gates — research checkpoint

This is incomplete research, not a completed port. No runtime changes yet.
Binary: RotWK 2.01, MD5 daed3668006cd90f01c34e5a7da1901f.
Read using tools/mod/rw.py dis in the sandbox.

## Confirmed branches

* RW 0x671ED8–0x671F15, read linearly from 0x671ED8: absent logic
  pointer (0xDE412C) or absent held object (receiver +0xFC) returns false.
  Correction to the first checkpoint: receiver is the drawable, not the object;
  see caller proof below. Do not implement this as Object::clientHooks()!=null.
  0x671EF2 passes model condition 0x25 to 0x694154. If true, return false;
  otherwise unsigned current frame at logic +0x40 must be >= drawable +0x384.
* RW 0x694154–0x694180: first checks the object's condition via 0x46E918;
  failing that, resolves its horde via 0x693A1A(0) and tests that condition.
* RW 0x671F16–0x671F31: if logic exists, charge timeout becomes current
  frame plus template +0x568. This neighboring behavior must remain intact.
* RW move helper 0x8DE838, read linearly through 0x8DEB2E so far:
  0x8DE876 classifies enter-state messages. At 0x8DE8F3–0x8DE90E,
  planning replacement requires non-enter-state, controlling player,
  player +0x770 == 2, and false third argument. 0x8DE924 obtains MiscAudio;
  +0xE0 is copied into the choice through 0x8DDA33 at 0x8DE94B, then exits.
* RW 0x8DEA22–0x8DEA5C: absent AI (+0x260) exits. Movement is
  0x664485() OR AI byte +0x3B1. Only non-enter-state orders test global
  0xDE4830 +0x8B0; if set and moving, exit. This is NOT an unconditional
  busy-unit mute. Earlier special choice remains populated on this return.

## Donor cross-check (not authority for RW)

DONOR BFME2 0x004D9CF3, Code/GameEngine/Source/GameClient/Drawable/PickAndPlayUnitVoiceResponse.cpp:
attack helper has planning replacement before attack-specific voices.
The same file's doPickAndPlayForMoveCommand shows the movement gate and
identifies its global as TheInGameUI, NOT GameData. Its +0x8B0 is
m_waypointMode in Code/GameEngine/Source/GameClient/InGameUICtor.cpp.
This contradicts the provisional GameData label in S-704; confirm the RW
identity and callers before choosing any API or field name in the port.

## Remaining research before implementation

Read the rest of the move helper, attack and enter helpers and callers;
confirm third argument semantics and RW global/class identities (including
object/drawable ownership); trace planning sound parser and live wiring.
Player::orderMode already exists; do not add duplicate simulation state.
Check existing drawable presence and waypoint-mode services and all callers.
Retrieve Ennorath 1.9.8.3 INI before making data claims.
Add PORT-462 tests only after expectations and integration are confirmed.

## Second checkpoint: caller proof and additional branches

RW picker 0x8DEDBB read linearly (1450 instructions). At 0x8DF045,
object EDI is passed to 0x70E013; returned EBX must be nonnull (0x8DF04E).
0x8DF065 pushes EBX, 0x8DF066 pushes object EDI, and 0x8DF067 calls
choice constructor 0x8DD819 (previously read). Constructor stores object
at choice +0x10, EBX at +0x14. Attack helper loads choice +0x14 at
0x8DDF50 into its local -0x20; 0x8DE15C loads that local as the receiver
of 0x671ED8. Thus +0xFC in the charge helper is the drawable's object,
NOT Object's drawable. DONOR BFME2 0x00271BDE
(Code/GameEngine/Source/GameClient/Drawable.cpp) corroborates this:
TheGameLogic, m_fc, m_fc->condition(0x25), drawable deadline m_384.
The port already operates on live Objects, not unbound drawables. Adding
an Object::clientHooks presence test would be a different condition and
would incorrectly mute headless objects. Need finish binding proof before
choosing whether any charge-gate runtime change is actually required.

RW attack helper now read through return 0x8DE1BC. Planning branch at
0x8DDFCD–0x8DE04C matches move's conditions. Weapon override precedes
charge (0x8DE0F4–0x8DE150); charge helper is called at 0x8DE15F; air
fallback follows at 0x8DE17D, base at 0x8DE19C. No waypoint gate in attack.
RW move helper remainder read through 0x8DED5B: existing camp, elevation,
while-attacking, wall-climbing and base rules follow the movement gate.

RW 0x8DEE0E–0x8DEE1A: the helpers' third bool is original message ==
0x46A (not generic force attack). Passed from picker at 0x8DFC77 and
0x8DFCA8 / 0x8DFCC1. RW 0x8DF416–0x8DF464 has the planning branch in
enter handling too. Another planning consumer exists for a command-button
case (0x8DFB11–0x8DFB5F); inspect its scope before claiming full mechanism.

Existing port services: Player::orderMode(), InGameUI::isInWaypointMode(),
AIUpdate::isMoving(), AIMover::isWaitingForPath(), MiscAudio::get().
CommandXlat already cites RW 0x8200FA for waypoint mode. RW instruction
at 0x8200FA sets receiver +0x8B0, 0x82011D clears it (read this range).
No new simulation state is needed for these services. Live HUD creates
one UnitVoiceResponse and uses it for translator, stance and object voices;
new state must not be refreshed only on one of those routes.

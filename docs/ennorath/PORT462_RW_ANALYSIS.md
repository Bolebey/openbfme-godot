# Port 462: unit voice gates — research checkpoint

This is incomplete research, not a completed port. No runtime changes yet.
Binary: RotWK 2.01, MD5 daed3668006cd90f01c34e5a7da1901f.
Read using tools/mod/rw.py dis in the sandbox.

## Confirmed branches

* RW 0x671ED8–0x671F15, read linearly from 0x671ED8: absent logic
  pointer (0xDE412C) or absent object drawable (+0xFC) returns false.
  0x671EF2 passes model condition 0x25 to 0x694154. If true, return false;
  otherwise unsigned current frame at logic +0x40 must be >= object +0x384.
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

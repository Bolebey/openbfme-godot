# Port 464 (issue #464, port of parked Port 197): the per-frame steering of a summoned helper

Read from `game.dat` RotWK 2.01 (md5 `daed3668…`) with `tools/mod/rw.py dis`, linearly from each function's start.
BFME2 1.06 (Open-BFME-2) has no `AISpecialPowerUpdate` decision classes: the only donor hit is the module itself
(`Code/GameEngine/Source/GameLogic/Object/Update/AISpecialPowerUpdate.cpp`), so every fact below is RW.

## The base slot 1 (what the port has today)

RW 0x993019 — the decision base class (RW 0x992FD2, members `+0x04` kind, `+0x08` power type, `+0x0C` button,
`+0x10` counter, `+0x14` range, `+0x18` radius, `+0x1C` fired):

```
mov eax, [ecx]            ; vtable
inc dword ptr [ecx+0x10]  ; ++counter
call dword ptr [eax+0x14] ; slot 0x14 (empty for every type in this batch)
ret 4
```

The port already does the `++counter` (`AISpecialPowerUpdate.cpp`, `update()`). The three types below **override**
slot 1 and chain to the base at the end (`call 0x993019`), so their work happens before the counter grows:

| type | slot 1 | chains to the base at |
|---|---|---|
| TARGETAOE_SUMMON | RW 0x9EB961 | RW 0x9EBBDC |
| ASSIST_BATTLE_BUFF / _DEBUFF | RW 0x9E8A42 | end of function |
| SHROUD_REVEAL | RW 0x9E7F0F | end of function |

## The shared prologue (identical in all three functions)

```
if (!obj) goto base;                                    RW 0x9EB973 / 0x9E8A54 / 0x9E7F21
player = obj->getControllingPlayer();   RW 0x68B678    RW 0x9EB984 / 0x9E8A62 / 0x9E7F2F
if (!player) goto base;
if (player-><+0x5C> != 1) goto base;                    RW 0x9EB999 / 0x9E8A7A / 0x9E7F47
if (this->helperName.isEmpty()) {   RW 0x401E64 (true = null or word[ptr+4]==0)
    button = this->button (+0x0C); if (!button) skip;
    template = button-><+0x44>;  if (!template) skip;
    module = obj->findModule(template);   RW 0x68C26D
    if (module) name = module->vslot[0x5C]();   // an AsciiString, stored when ptr && word[ptr+4]
    if (non-empty) this->helperName = name;      // member: +0x2C (SUMMON), +0x30 (ASSIST), +0x50 (SHROUD)
}
if (this->helperId && TheGameLogic) {             // RW 0xDE412C
    helper = TheGameLogic->findObjectByID(this->helperId);   RW 0x449681
    if (helper) goto steer;
    this->helperId = 0 and the type's other state cleared;   // RW 0x9EBA54, 0x9E8B34..
}
helper = findHelper(player, this->helperName, TRUE);         // RW 0x99316A
if (!helper) goto base;
this->helperId = helper-><+0x74>;                            // the object's id, cached
steer:
if (!helper->ai (Object + 0x260)) goto base;                 // RW 0x9EBA8C / 0x9E8B7D
if (helper->template (Object + 0x04)-><+0x108> & 8) goto base;  // template flag byte
... type body ...
base: call 0x993019
```

## RW 0x99316A — the helper lookup

`Object *f(Player *player, AsciiString name, Bool heroOnly)`; the three callers are RW 0x9E802E (SHROUD),
0x9E8B64 (ASSIST_BATTLE), 0x9EBA77 (SUMMON).

* the player's AI record: ThePlayerList (RW 0xDE4938) + 0xA3C registry, RW 0x6A9999 (the record whose `Object + 0x54`
  matches), returns the record;
* first list: `[record]` -> `+4` / `+8` (RW 0x72B229 returns `this+4`); if nothing was found, the second list
  `record + 0x14` (RW 0x8FF2D7). Both are vectors of ObjectIDs — the AIPlayer's army and hero lists;
* each entry: `findObjectByID` (RW 0x449681 with TheGameLogic); skip a null, skip when the template's
  `<+0x108>` has bit 0x80; when `heroOnly`, require `<+0x113> & 4`;
* the first whose template (`Object + 0x04`) name (`+0x64`, AsciiString compare RW 0x4065AA) equals `name` is
  returned.

## RW 0x9EB961 — TARGETAOE_SUMMON's body

```
helperWeapon = helper->ai-><+0x1F0>;              RW 0x68B31D
if (!helperWeapon || helperWeapon-><+0x40> > 0)   RW 0x5E36F7 (fld [ecx+0x40]), RW 0x9EBAC4
    goto ranged;                                  // 0x9EBB4F
// melee: keep the helper's own feet on the victim
victim = obj->ai->getCurrentVictim();             RW 0x668303
if (!victim) goto base;
d2 = (helper->x - victim->x)^2 + (helper->y - victim->y)^2
if (d2 <= 25.0 (RW 0xC69574)) goto base;
helper->ai->aiMoveToPosition(&victim->position, 0);   RW 0x66C4CA
goto base;
ranged:                                           // 0x9EBB4F
victim = obj->ai->getCurrentVictim();
if (!victim) goto base;
if (!helper->getGoalPosition(&goal)) goto base;    RW 0x68B411 = [Object + 0xA4] -> RW 0x8E1BD4
d2 = (goal.x - victim->x)^2 + (goal.y - victim->y)^2
if (d2 <= 900.0 (RW 0xBE5AF8)) goto base;
helper->ai->aiMoveToPosition(&victim->position, 0);   RW 0x66C4CA
```

`Object + 0xA4` is the object's path-goal holder (`RW 0x8E1BD4` reads the sub-object whose magic at `+0x1C` is
0xFFF5D3D6 — the donor names that class `PathfinderPosGoalManager`, `PathfinderPosGoalManager.cpp:75`; the
donor's `+0x24`/`+0x28` fields are the goal). So the ranged branch re-issues the move only when the path goal
has drifted 30 units from the victim; the melee branch uses the helper's own position (5 units).

## RW 0x9E8A42 — ASSIST_BATTLE_BUFF / _DEBUFF's body (in progress)

Same prologue; helper name `+0x30`, cached helper id `+0x34`, and the class keeps more state used by the body:
`+0x38`, `+0x3C` (ObjectIDs looked up with RW 0x449681, `<+0x458>` bit 0 = an effectively dead object clears
`+0x3C`), `+0x40` (a counter compared against `(end - begin) / 12` of the vector at `+0x44`), `+0x50`.

## RW 0x9E7F0F — SHROUD_REVEAL's body (in progress)

Same prologue; helper name `+0x50`.

## Next

1. finish 0x9E8A42 and 0x9E7F0F linearly to their ends;
2. name the special power module's vslot 0x5C (the helper template name) in port terms;
3. port the shared prologue once plus the three bodies, with the new decision state in crc / xfer (PROCESS 15.5).

# Port 292: HordeContain investigation

## Carry-over template

The HordeContain create path RW 0x64B5D5 reaches constructor RW 0x8727F9, which installs the HordeContainInterface table RW 0xC5B1F8 at object +0x11C (RW 0x872873). Table slot +0x274 is RW 0x86D6FD: it copies the AsciiString at module-data +0x280, not a Boolean or a template pointer. The field row is RW 0xC5BDF0, parsed by RW 0x42EE5E; its name is LivingWorldOverloadTemplate. The cavalry constructor installs table RW 0xC5BE98 at the same interface offset (RW 0x8798A1), and its corresponding slot also points to RW 0x86D6FD.

Object's carry-over record builder RW 0x6917BB checks HERO first (RW 0x6917DF), then HORDE (RW 0x69184B). For a non-hero horde it obtains the interface RW 0x68C866, calls slot +0x274 at RW 0x691869 and tests the returned string for non-emptiness through RW 0x401E64. If present it calls the same slot again and copies the string to the record at RW 0x6918A5..0x6918B6. If absent, or if there is no interface, the template's own name is used at RW 0x6918C7..0x6918DE. Hero handling remains the preceding branch, not a second horde path. The caller for campaign victory is RW 0x5EBAEE; the existing port is `CampaignCarryover::recordOf` and `onCurrentMissionWon`.

The older field triage incorrectly attributes RW 0x881A5E to HordeContain: the reader is in the RiderChangeContain path, not a read of the horde field. Its factory RW 0x64BA11 calls constructor RW 0x881501 (RW 0x64BA3B); that installs its contain table RW 0xC5DFA8 at +0x20 (RW 0x88153F), whose slot +0x5C contains RW 0x881A5E. Thus the +0x280 byte at RW 0x881A95 belongs to RiderChangeContain's data, not HordeContainModuleData. The record builder above has an independent, complete interface-to-vtable chain and proves a real read of the horde field. BFME2 donor search for the field name in Code yielded no named counterpart; the evidence here is RotWK disassembly, not donor inference.

The other confirmed reader of slot +0x274 is AIWoTRForfeitTactic update RW 0x9B7458: at RW 0x9B770E / 0x9B7732 it retrieves the overload name and compares it against RW 0xC89C78. A match skips owned upgrade cost (RW 0x9B7782); a nonmatch or empty name includes it. `AITacticalAI.cpp::forfeitScore` instead uses the object's name, explicitly marked INFERENCE in S-7187. That scorer is owned by open Port 33 (fork issue 105), so it is not changed here; see S-7518.

## Rank release field

`RanksToJustFreeWhenAttacking` already has a detailed proof and regression tests in `docs/ennorath/PORT81_RW_ANALYSIS.md`, `docs/STOPS.md` S-7213 and `engine/tests/test_port_81.cpp`: parser row RW 0xC5BB90, data offset +0x1C4, constructor/copy sites RW 0x878F5D, 0x878C91, 0x878D97, and the established whole-text displacement scan found no behaviour reader. Its neighbour `RanksToReleaseWhenAttacking` at +0x1B8 is read by RW 0x875221, RW 0x87545C and RW 0x875562. Do not invent a second release path from the field name.

## Further work

The other uncited functions still require classification against their callers and the other open ownership boundaries. In particular RW 0x870D4E iterates the contained list and the entering-member map, optionally filters each object, skips missing drawables, and invokes Drawable RW 0x672B96 then RW 0x67093E. The apparent slot +0x1F0 caller RW 0x48EAA3 is **not** a horde call: its function RW 0x48EA29 invokes slots on `esi` in a loop over global UI state and belongs to W3DInGameUI. Other candidates RW 0x59B124 (refcounted elements in an unrelated collection), RW 0x743B6D (object +0x260), and RW 0x83B148 (global camera interface) also do not establish a horde call. Its caller and client event path still need proof; do not introduce a simulation-to-drawable direct call (the existing thread boundary is `ObjectClientHooks`).

## Individually checked small entries

The table is deliberately partial: the other rows in the function map still need a behaviour-by-behaviour comparison, not an assumed verdict from `none`.

| RW start | First-instruction body / outcome | Port location or scope |
|---|---|---|
| 0x872A69 | Returns the HordeContain class-name string pointer; not simulation behaviour. | Module registry plumbing. |
| 0x872A8D | Stores the argument at interface +0x160; setter, reader not established. | Needs its caller chain. |
| 0x872A9A | Returns interface +0x160; getter. | Needs its caller chain. |
| 0x872AAE | Returns interface +0x1EC, the last-member-entered frame. | Already ported: `HordeContain::lastMemberEnteredFrame`, `HordeGarrisonLink.cpp`. |
| 0x872ACB | Returns interface +0x150; getter. | Needs its caller chain. |
| 0x872AD2 | Stores the argument at interface +0x150; setter, reader not established. | Needs its caller chain. |
| 0x872AE7 | Returns interface +0x188, the garrisoned flag. | Already ported: `HordeContain::isGarrisoned`, `HordeGarrisonLink.cpp`. |
| 0x872AEE | Returns interface +0x16C; getter. | Needs its caller chain. |
| 0x872AF5 | Stores the argument at interface +0x18C; setter, reader not established. | Needs its caller chain. |
| 0x872B02 | Returns interface `this - 0x11C`; base-pointer adjustment, not simulation behaviour. | Interface plumbing. |
| 0x872B24 | Interns the HordeContain module name and returns its key; not simulation behaviour. | Module registry plumbing. |
| 0x872B76 | Clears interface +0x188 and wakes the containing update. | Already ported: `HordeContain::setGarrisoned`, `HordeGarrisonLink.cpp`. |
| 0x8736EF | Scalar deleting destructor (map tier A); not gameplay behaviour. | Lifecycle. |
| 0x86C77D | Initialises module +0x2C5 to the inverse of module-data `ThisFormationIsTheMainFormation` +0x1D8. | Missing: `HordeContain::onCreate` is empty; snapshot / mismatch recovery is S-7520. |
| 0x86C8F2 | Compares saved module +0x2C5 with inverse module-data +0x1D8; mismatch calls horde interface +0x60 (`toggleFormation` RW 0x86C7F6). | Missing formation restoration; S-7520. |
| 0x86D056 | Interface slot +0x30: this-adjusting thunk to RW 0x86C997, which passes each `AttributeModifiers` entry (module-data +0x22C) and -1 to the argument's `Object::addAttributeModifier` RW 0x68F1A8. | Missing joining-member path S-1600; the separately implemented `HordeFormation.cpp::applyFormationModifiers` is the distinct slot +0x1E4. The hand-off is within #552's membership area. |
| 0x8713F6 | Horde mean position; both contained and entering-member lists. | Partially ported in `AttachUpdate.cpp`; entering-member list is owned by #552, S-1361. |
| 0x86D6FD | Copies module-data +0x280 as a string. | Ported here: `HordeContain::livingWorldOverloadTemplate`; remaining forfeit caller S-7518. |
| 0x86DF44 | Last-member death / periodic split check. | Already ported: `HordeContainRuntime.cpp`, Port 265. |
| 0x86E13E | Iterates the contained list, searches the module +0x17C id map and calls horde slot +0x2C for each untracked member. | Membership hand-off belongs to #552; full equivalence unverified. |
| 0x86F60A | Checks the local player and `UseSimpleHordeDecals`, then invokes Drawable RW 0x672F0D for the horde or its contained / entering members. | Client presentation and membership path S-7505 / #552; full equivalence unverified. |
| 0x870AEE | Returns false if a contained member not in interface +0x54 is more than 10 units away in 2D; otherwise true. | Member / entering map owned by #552; full equivalence unverified. |
| 0x8710A8 | For each contained member not in interface +0x54, calls its AI slot +0x218 with both arguments if it has AI. | Member hand-off #552; full equivalence unverified. |
| 0x871110 | Calls `FXList::doFXObj` RW 0x4B1B5A for contained members and live objects resolved from interface +0x54 ids. | Member / entering map and client event boundary; full equivalence unverified, S-7505 / #552. |

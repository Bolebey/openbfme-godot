# Port 292: HordeContain investigation

## Carry-over template

The HordeContain create path RW 0x64B5D5 reaches constructor RW 0x8727F9, which installs the HordeContainInterface table RW 0xC5B1F8 at object +0x11C (RW 0x872873). Table slot +0x274 is RW 0x86D6FD: it copies the AsciiString at module-data +0x280, not a Boolean or a template pointer. The field row is RW 0xC5BDF0, parsed by RW 0x42EE5E; its name is LivingWorldOverloadTemplate. The cavalry constructor installs table RW 0xC5BE98 at the same interface offset (RW 0x8798A1), and its corresponding slot also points to RW 0x86D6FD.

Object's carry-over record builder RW 0x6917BB checks HERO first (RW 0x6917DF), then HORDE (RW 0x69184B). For a non-hero horde it obtains the interface RW 0x68C866, calls slot +0x274 at RW 0x691869 and tests the returned string for non-emptiness through RW 0x401E64. If present it calls the same slot again and copies the string to the record at RW 0x6918A5..0x6918B6. If absent, or if there is no interface, the template's own name is used at RW 0x6918C7..0x6918DE. Hero handling remains the preceding branch, not a second horde path. The caller for campaign victory is RW 0x5EBAEE; the existing port is `CampaignCarryover::recordOf` and `onCurrentMissionWon`.

The older field triage incorrectly attributes RW 0x881A5E to HordeContain: the reader is in the RiderChangeContain path, not a read of the horde field. Its factory RW 0x64BA11 calls constructor RW 0x881501 (RW 0x64BA3B); that installs its contain table RW 0xC5DFA8 at +0x20 (RW 0x88153F), whose slot +0x5C contains RW 0x881A5E. Thus the +0x280 byte at RW 0x881A95 belongs to RiderChangeContain's data, not HordeContainModuleData. The record builder above has an independent, complete interface-to-vtable chain and proves a real read of the horde field. BFME2 donor search for the field name in Code yielded no named counterpart; the evidence here is RotWK disassembly, not donor inference.

The other confirmed reader of slot +0x274 is AIWoTRForfeitTactic update RW 0x9B7458: at RW 0x9B770E / 0x9B7732 it retrieves the overload name and compares it against RW 0xC89C78. A match skips owned upgrade cost (RW 0x9B7782); a nonmatch or empty name includes it. `AITacticalAI.cpp::forfeitScore` instead uses the object's name, explicitly marked INFERENCE in S-7187. That scorer is owned by open Port 33 (fork issue 105), so it is not changed here; see S-7518.

A supplementary byte-seeded Capstone scan over every executable PE section looks for memory `call`/`jmp` operands with displacement +0x274: exactly four candidate instructions, RW 0x691869 / 0x6918A5 and RW 0x9B770E / 0x9B7732, all in those two readers. Scanning +0x280 memory-displacement candidates in the horde address region also finds RW 0x87293C and RW 0x87848B on the **module instance** (the latter is its xfer), RW 0x878D3B (data destructor), RW 0x8790D7 (data constructor), and RW 0x881A95 / 0x881D96 on RiderChangeContain; a spurious overlapping decode at 0x86D70D starts inside the actual `add eax, 0x280` of RW 0x86D6FD. The byte-seeded scan is a candidate inventory, not proof against inlining, pointer arithmetic, register-loaded vslots, or disassembly overlap: additional data-flow attribution outside this region is still required before claiming every reader. The scanned retail bytes remain uncommitted.

## Rank release field

`RanksToJustFreeWhenAttacking` already has a detailed proof and regression tests in `docs/ennorath/PORT81_RW_ANALYSIS.md`, `docs/STOPS.md` S-7213 and `engine/tests/test_port_81.cpp`: parser row RW 0xC5BB90, data offset +0x1C4, constructor/copy sites RW 0x878F5D, 0x878C91, 0x878D97, and the established whole-text displacement scan found no behaviour reader. Its neighbour `RanksToReleaseWhenAttacking` at +0x1B8 is read by RW 0x875221, RW 0x87545C and RW 0x875562. Do not invent a second release path from the field name.

## Further work

The other uncited functions still require classification against their callers and the other open ownership boundaries. In particular RW 0x870D4E iterates the contained list and the entering-member map, optionally filters each object, skips missing drawables, and invokes Drawable RW 0x672B96 then RW 0x67093E. A byte-seeded search for indirect `call`/`jmp` with displacement +0x1F0 in all executable sections yielded only four candidates. RW 0x48EAA3 is **not** a horde call: its function RW 0x48EA29 invokes slots on `esi` in a loop over global UI state and belongs to W3DInGameUI. RW 0x59B124 acts on refcounted elements of another collection; RW 0x743B6D belongs to AI guard state's on-exit RW 0x743B40 (`AITNGuard.cpp`) and calls the owner's AI at Object +0x260 with no four decal arguments; RW 0x83B148 acts on the global camera interface. These results are not proof that the decal has no caller: a direct call, another vtable displacement, or a register-loaded slot can evade that search. Its caller and client event path still need proof; do not introduce a simulation-to-drawable direct call (the existing thread boundary is `ObjectClientHooks`).

## Individually checked small entries

The table is deliberately partial: the other rows in the function map still need a behaviour-by-behaviour comparison, not an assumed verdict from `none`.

| RW start | First-instruction body / outcome | Port location or scope |
|---|---|---|
| 0x872A69 | Returns the HordeContain class-name string pointer; not simulation behaviour. | Module registry plumbing. |
| 0x872A8D | Stores the argument at interface +0x160 = module +0x27C, the banner countdown. | `HordeContain::m_bannerCountdown`, set in `HordeBanner.cpp`; direct setter's callers still need comparison. |
| 0x872A9A | Returns interface +0x160 = module +0x27C, the banner countdown; getter. | `HordeContain::m_bannerCountdown`; no standalone gameplay algorithm. |
| 0x872AAE | Returns interface +0x1EC, the last-member-entered frame. | Already ported: `HordeContain::lastMemberEnteredFrame`, `HordeGarrisonLink.cpp`. |
| 0x872ACB | Returns interface +0x150 = module +0x26C, banner carrier id; getter. | `HordeContain::bannerCarrier`; `SpecialAbilityModules.cpp` reads it. No standalone gameplay algorithm. |
| 0x872AD2 | Stores argument at interface +0x150 = module +0x26C, banner carrier id. | `HordeBanner.cpp::handOverBannerCarrier` assigns `m_bannerCarrier`; full setter caller comparison remains open. |
| 0x872AE7 | Returns interface +0x188, the garrisoned flag. | Already ported: `HordeContain::isGarrisoned`, `HordeGarrisonLink.cpp`. |
| 0x872AEE | Returns interface +0x16C = module +0x288; getter. | Donor stores an object id there (BFME2 `HordeContainIface11CSlots.cpp`), exact RW role and port equivalence still need verification. |
| 0x872AF5 | Stores the argument at interface +0x18C = module +0x2A8. | Donor treats this as initial-payload survival percent (`HordeContain_createPayload.cpp`); RW reader and port comparison remain to be checked. |
| 0x872B02 | Returns interface `this - 0x11C`; base-pointer adjustment, not simulation behaviour. | Interface plumbing. |
| 0x872B24 | Interns the HordeContain module name and returns its key; not simulation behaviour. | Module registry plumbing. |
| 0x872B76 | Clears interface +0x188 and wakes the containing update. | Already ported: `HordeContain::setGarrisoned`, `HordeGarrisonLink.cpp`. |
| 0x8736EF | Scalar deleting destructor (map tier A); not gameplay behaviour. | Lifecycle. |
| 0x86C0BC | Returns -1 for null, otherwise the argument's indicator colour via Object RW 0x6916CD. | Underlying `Object::getIndicatorColor` exists; this wrapper's callers and equivalence still need verification. |
| 0x86C77D | Initialises module +0x2C5 to the inverse of module-data `ThisFormationIsTheMainFormation` +0x1D8. | Missing: `HordeContain::onCreate` is empty; snapshot / mismatch recovery is S-7520. |
| 0x86C8F2 | Compares saved module +0x2C5 with inverse module-data +0x1D8; mismatch calls horde interface +0x60 (`toggleFormation` RW 0x86C7F6). | Missing formation restoration; S-7520. |
| 0x86D056 | Interface slot +0x30: this-adjusting thunk to RW 0x86C997, which passes each `AttributeModifiers` entry (module-data +0x22C) and -1 to the argument's `Object::addAttributeModifier` RW 0x68F1A8. | Missing joining-member path S-1600; the separately implemented `HordeFormation.cpp::applyFormationModifiers` is the distinct slot +0x1E4. The hand-off is within #552's membership area. |
| 0x86D3F3 | Returns true if any contained object has Object +0x110 bit 29. | Member-status query; meaning and port equivalence still need verification. |
| 0x86D438 | For each live contained member with AI, requires AI slot +0x1BC and current victim RW 0x668303 matching the argument, with an additional argument-contain test for hordes. | Member AI / containment #552 and S-7505; full equivalence unverified. |
| 0x86D4CD | For each live contained member with AI, looks for AI slot +0x1BC and a current victim matching the argument or the argument's horde contain test. | Member AI / containment #552 and S-7505; full equivalence unverified. |
| 0x86D5BB | Returns true if any live contained member with AI answers true at AI slot +0x1BC. | Member AI / containment #552 and S-7505; full equivalence unverified. |
| 0x86D7E6 | Walks contained members and returns false if a member's AI +0x140 exists and AI +0x1FC equals 4; otherwise true. | Member-state query; meaning and port equivalence still need verification. |
| 0x8713F6 | Horde mean position; both contained and entering-member lists. | Partially ported in `AttachUpdate.cpp`; entering-member list is owned by #552, S-1361. |
| 0x86D6FD | Copies module-data +0x280 as a string. | Ported here: `HordeContain::livingWorldOverloadTemplate`; remaining forfeit caller S-7518. |
| 0x86DF44 | Last-member death / periodic split check. | Already ported: `HordeContainRuntime.cpp`, Port 265. |
| 0x86E13E | Iterates the contained list, searches the module +0x17C id map and calls horde slot +0x2C for each untracked member. | Membership hand-off belongs to #552; full equivalence unverified. |
| 0x86F60A | Checks the local player and `UseSimpleHordeDecals`, then invokes Drawable RW 0x672F0D for the horde or its contained / entering members. | Client presentation and membership path S-7505 / #552; full equivalence unverified. |
| 0x86F8BA | For non-null argument, searches contained and interface +0x54 objects for Object +0x46C equal to it. | Member lookup #552; semantics and full equivalence unverified. |
| 0x86F9CA | Returns true if any id in interface +0x54 resolves to a live object. | Entering-member map #552 / S-7505; full equivalence unverified. |
| 0x86FE0F | Selects a random contained object, or if the list is empty a random interface +0x54 id resolved through RW 0x449681; empty lists return null. | Member lookup #552 / S-7505; full equivalence unverified. |
| 0x870655 | Resolves the banner carrier id at interface +0x150 = module +0x26C through `findObjectByID` RW 0x449681. | Getter; `HordeContain::bannerCarrier` exposes the id, and `HordeContainRuntime.cpp` resolves `m_bannerCarrier` via logic. Direct caller comparison remains open. |
| 0x870AEE | Returns false if a contained member not in interface +0x54 is more than 10 units away in 2D; otherwise true. | Member / entering map owned by #552; full equivalence unverified. |
| 0x8710A8 | For each contained member not in interface +0x54, calls its AI slot +0x218 with both arguments if it has AI. | Member hand-off #552; full equivalence unverified. |
| 0x871110 | Calls `FXList::doFXObj` RW 0x4B1B5A for contained members and live objects resolved from interface +0x54 ids. | Member / entering map and client event boundary; full equivalence unverified, S-7505 / #552. |

## Additional first-instruction checks

These entries are not a blanket claim that the entire #552 member mechanism is ported; where the comparison is unfinished that is stated explicitly.

| RW start | Body / status | Scope or port |
|---|---|---|
| 0x86BD7D | Stores three coordinate floats at interface +0x1DC and marks +0x1E8 valid. Missing hand-off. | S-7519, factory exit and #650. |
| 0x86BD98 | If valid, clears +0x1E8 and copies the saved coordinates to the output, returning true; otherwise false. Missing hand-off. | S-7519, factory exit and #650. |
| 0x86BDC0 | Reads argument +0x38 and forwards it to receiver vslot +0xB0. | Wrapper; receiver/caller identity and port comparison unresolved. |
| 0x86BE2E | Delegates to the owner's AI vslot +0x1C8, false if no AI. | Query; caller/port comparison unresolved. |
| 0x86BE49 | Delegates to the owner's AI vslot +0x1CC, false if no AI. | Query; caller/port comparison unresolved. |
| 0x86BE64 | Requires three non-null arguments then calls RW 0x6CC653 with 0.0 and 1. | Weapon helper; precise caller and equivalence unresolved. |
| 0x86C192 | Sets owner orientation using RW 0x70C31E, marks interface +4 dirty, wakes update RW 0x850C32. | Port comparison and caller unresolved. |
| 0x86C758 | Rejects argument with template +0x115 bit 0x20; otherwise forwards three arguments to RW 0x86A002. | Contain acceptance, S-7505 / #552; exact equivalence unresolved. |
| 0x86C8B9 | Null-guarded delegation from owner +0x27C to that container's +0x258 vslot +0x68. | Containment hand-off S-7505 / #552; exact equivalence unresolved. |
| 0x86C9E7 | Returns address of module-data +0x1A4 (`SplitHorde` vector), no mutation. | Getter; parser in `HordeContain.cpp`, storage `HordeContainModuleData::m_splitHorde`; callers and equivalence still require confirmation. |
| 0x86C9F3 | Returns address of module-data +0x18C (`RankInfo` vector), no mutation. | Getter; parser in `HordeContain.cpp`, storage `HordeContainModuleData::m_rankInfo`; callers and equivalence still require confirmation. |
| 0x86D1FE | If the horde AI is moving RW 0x664485, calls RW 0x662A0E for each contained member's AI, then the horde AI. | Member-AI mechanism #552 / #650; exact equivalence unresolved. |
| 0x86F6E1 | Calls RW 0x690B11 on contained and entering members, then if drawable and locally controlled flushes model conditions RW 0x67449C(0). | Selection/presentation and member list S-7505 / #552; exact equivalence unresolved. |
| 0x86FEAD | Uses callback RW 0x86C79A over members and resolves the chosen id: callback selects greatest experience excluding template bit +0x11C / 0x10. | Member selection S-7505 / #552; exact equivalence unresolved. |
| 0x86FFD2 | Requires at least one member, the requested template to equal the alternate, and non-main alternate's MinimumHordeSize. | Existing `HordeFormation.cpp::registerHandlers` checks `alternateFormationTemplate` and `canToggleFormation`; formation swap remains S-589. |
| 0x870D4E | Traverses contained objects and entering ids, applying optional filter RW 0x7640C1 with member and horde's controlling player (RW 0x68B678); calls Drawable `setTerrainDecal(type)` RW 0x672B96 and `setTerrainDecalFadeTarget(target, rate)` RW 0x67093E. | S-7505 / #552 lists this function; caller and ordered presentation path unresolved, no simulation-to-client shortcut. |
| 0x87104A | With argument flags bit 0x10, visits interface +0x150 ids through callback; otherwise delegates to RW 0x8666A1. | Member-list traversal #552; exact equivalence unresolved. |
| 0x87159F | Sets interface +0x1CC then clears tree at +0x1C0 through RW 0x8664EA. | State/ownership and callers unresolved. |
| 0x871844 | Calls interface +0xE0, frees each +0x270 vector entry, clears vector and tail-calls RW 0x86708F. | Lifecycle/contain cleanup S-7505 / #552; exact equivalence unresolved. |
| 0x8762DD | If member has AI, busy vslot +0x1C4 causes idle command RW 0x5E821A and true; otherwise returns AI idle vslot +0x1B8. | Callback within formation/member mechanism S-7505 / #552; exact port equivalence unresolved. |
| 0x87737A | Contain-interface this-adjusting thunk to RW 0x87566B, upgrade propagation to horde members. | `Object.cpp::giveUpgradeSelf`, `HordeGarrisonLink.cpp`; second member list remains noted in `UpgradeModuleClasses.cpp`, S-7505 / #552. |
| 0x8780E4 | Snapshot transfer, including saved formation identity module +0x2C5. | `HordeContain::xfer` handles existing state, but identity and recovery are missing, S-7520; remainder requires comparison. |
| 0x86CA78 | Checks wall / ramp cells, selects a nearby wall and its SiegeDockingBehavior, adjusts owner orientation and wakes update. Donor BFME2 `HordeContainIface11CSlots.cpp::rva00469967`; RW 0x86CA78..0x86CC83. | Wall/pathfinder and formation member mechanism, S-7505 and #552's S-1500; comparison to the port remains open. |
| 0x86F7A3 | Saves the horde AI state id from RW 0x660AC1 at interface +0x80, then traverses contained and entering members, clears MOVING for qualifying stopped members and calls module vslot +0x80. | Member movement / hand-off #552 and #650, S-7505; comparison remains open. |
| 0x86F93F | Walks the +0x54 id map, resolves members and tests AI state RW 0x660AC1 and Object +0x110 bit 29; returns false if none found or a disqualifying member appears. | Member AI / entering list #552, S-7505; exact semantic name and port equivalence remain open. |
| 0x8707A0 | Walks contained and entering objects, sums translations of available drawable transforms, tracks object flags and proceeds with averaging / client calls. | Presentation plus entering-list ownership #552, S-7505; inspect the complete output and caller chain before claiming equivalence. |
| 0x871184 | Scans contained and entering members' module at Object +0x25C, slot +0x4C, resolves returned ids, preferring the argument id or its container. Donor `rva0046DEA1`. | Member / garrison #552, S-7505; exact port comparison open. |
| 0x871F8B | Traverses contained members, obtains each formation slot and adjusts destination RW 0x871897 before forwarding four arguments and positions to member AI RW 0x665777. | Member movement #552, S-7505; exact equivalence open. |
| 0x873262 | Traverses contained members, checks member AI/locomotor data, selects among three input lists and appends the member id through RW 0x5487BB. | Member / movement #552, S-7505; precise list semantics and equivalence open. |
| 0x87545C | Unless melee-attack gate RW 0x86C6EB holds, returns entering members, walks contained members with slot/rank mapping and `RanksToReleaseWhenAttacking` +0x1B8, finds a nearby enemy RW 0x701443 and issues member attack RW 0x66C536. | Member attack and return-to-formation S-7505 / #552; `HordeMemberPass.cpp::attackTargetNow` is a different RW 0x875221 path, not evidence this one is ported. |
| 0x875F18 | Uses member id and rank lookup RW 0x86C3E7, then applies or clears the rank's weapon condition bits through Object RW 0x691059 / RW 0x691106. | Rank/member hand-off S-7505 / #552; exact equivalence open. |
| 0x879106 | Calls RW 0x6998D0 for contained and resolved +0x54 objects; then follows owner RW 0x68C4A6 and, if present, calls RW 0x698DC9. | Member notification S-7505 / #552; exact meaning and equivalence open. |

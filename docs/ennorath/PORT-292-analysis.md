# Port 292: HordeContain investigation

## Carry-over template

The HordeContain create path RW 0x64B5D5 reaches constructor RW 0x8727F9, which installs the HordeContainInterface table RW 0xC5B1F8 at object +0x11C (RW 0x872873). Table slot +0x274 is RW 0x86D6FD: it copies the AsciiString at module-data +0x280, not a Boolean or a template pointer. The field row is RW 0xC5BDF0, parsed by RW 0x42EE5E; its name is LivingWorldOverloadTemplate. The cavalry constructor installs table RW 0xC5BE98 at the same interface offset (RW 0x8798A1), and its corresponding slot also points to RW 0x86D6FD.

Object's carry-over record builder RW 0x6917BB checks HERO first (RW 0x6917DF), then HORDE (RW 0x69184B). For a non-hero horde it obtains the interface RW 0x68C866, calls slot +0x274 at RW 0x691869 and tests the returned string for non-emptiness through RW 0x401E64. If present it calls the same slot again and copies the string to the record at RW 0x6918A5..0x6918B6. If absent, or if there is no interface, the template's own name is used at RW 0x6918C7..0x6918DE. Hero handling remains the preceding branch, not a second horde path. The caller for campaign victory is RW 0x5EBAEE; the existing port is `CampaignCarryover::recordOf` and `onCurrentMissionWon`.

The binary reader RW 0x881A5E described in the older field triage must not be attributed to HordeContain from the offset alone: its object and data layout and class must be established independently. The record builder above has a complete interface-to-vtable chain and proves a real read of the field. BFME2 donor search for the field name in Code yielded no named counterpart; the evidence here is RotWK disassembly, not donor inference.

## Rank release field

`RanksToJustFreeWhenAttacking` already has a detailed proof and regression tests in `docs/ennorath/PORT81_RW_ANALYSIS.md`, `docs/STOPS.md` S-7213 and `engine/tests/test_port_81.cpp`: parser row RW 0xC5BB90, data offset +0x1C4, constructor/copy sites RW 0x878F5D, 0x878C91, 0x878D97, and the established whole-text displacement scan found no behaviour reader. Its neighbour `RanksToReleaseWhenAttacking` at +0x1B8 is read by RW 0x875221, RW 0x87545C and RW 0x875562. Do not invent a second release path from the field name.

## Further work

The other uncited functions still require classification against their callers and the other open ownership boundaries. In particular RW 0x870D4E iterates the contained list and the entering-member map, optionally filters each object, skips missing drawables, and invokes Drawable RW 0x672B96 then RW 0x67093E. Its caller and client event path still need proof; do not introduce a simulation-to-drawable direct call (the existing thread boundary is `ObjectClientHooks`).

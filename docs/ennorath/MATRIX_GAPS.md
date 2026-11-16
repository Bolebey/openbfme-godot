# Failing matrix buttons and the RotWK functions their modules lack

Command: `tools/mod/matrix_gaps.py --base <ROTWK_INSTALL> --overlay <mod install> --matrix docs/ennorath/MATRIX.tsv --map docs/ennorath/RW_FUNCTION_MAP.tsv --out docs/ennorath/MATRIX_GAPS --inventory <rw_function_blocks.json> --game <game.dat>`

Failing matrix rows: 420; joined rows: 1351; module classes reached: 33.

A link is a name match: the module's fields name the button's SpecialPower or Upgrade. The map's status is exact RW citation in engine/src (a port without a citation reads `none`). The join says where to look first, not which function fails: each row's cause is still traced.

`specific none`: a `none` function reached from at most 3 module classes of the map (the class's own code); the other `none` rows are shared helpers that lack a citation everywhere. Functions of at most 12 bytes (class-name getters, one-field setters) are left out of it.

Recognised by their code with --game and left out of it too (793 functions of the inventory): getModuleNameKey (a short call of NameKeyGenerator RW 0x5487EC) and xfer functions (first indirect call Xfer vslot 0x28, xferVersion) and scalar deleting destructors (a short call of operator delete RW 0x42F6A0).

## Module classes by the failing buttons they touch

| class | buttons | map none | specific none | map partial | first specific none functions |
|---|---:|---:|---:|---:|---|
| SpecialPowerModule | 209 | 298 | 4 | 3 | 0x00654F8E@0 0x00896A32@0 0x008981BE@0 0x00896A70@1 |
| UnpauseSpecialPowerUpgrade | 164 | 21 | 4 | 0 | 0x008B9666@0 0x00697B6E@1 0x008B956C@1 0x008D280A@3 |
| WeaponFireSpecialAbilityUpdate | 144 | 178 | 3 | 12 | 0x00896156@0 0x00896370@0 0x006CA701@2 |
| AutoAbilityBehavior | 57 | 320 | 5 | 14 | 0x0085D3ED@1 0x006289E7@5 0x0062AB74@5 0x00629BCA@6 0x006A95E6@6 |
| ReplaceSelfUpgrade | 37 | 225 | 2 | 7 | 0x008BB21F@0 0x008BBAA9@1 |
| ToggleMountedSpecialAbilityUpdate | 30 | 352 | 2 | 16 | 0x008B42E6@0 0x008B1024@1 |
| OCLSpecialPower | 28 | 266 | 6 | 0 | 0x008C744B@0 0x008C72C3@1 0x008C7319@1 0x008C7A33@1 0x005EFB95@2 0x00896A70@2 |
| ModelConditionSpecialAbilityUpdate | 17 | 174 | 1 | 12 | 0x00894402@1 |
| ActivateModuleSpecialPower | 15 | 39 | 1 | 1 | 0x008D21A9@1 |
| RepairSpecialPower | 12 | 13 | 1 | 0 | 0x008CCB0B@1 |
| LevelGrantSpecialPower | 7 | 39 | 0 | 1 | - |
| SpecialAbilityUpdate | 6 | 353 | 2 | 16 | 0x00851E83@0 0x00852CA2@1 |
| ArrowStormUpdate | 5 | 186 | 8 | 12 | 0x00893D61@0 0x00894327@0 0x00893E4C@1 0x006A057C@2 0x006A1D75@2 0x006A253A@2 0x00852CA2@2 0x006A05C1@3 |
| ToggleDeploySpecialAbilityUpdate | 5 | 166 | 1 | 12 | 0x008B1BBE@1 |
| DoCommandUpgrade | 4 | 311 | 1 | 13 | 0x008B8D22@1 |
| ReplaceObjectUpdate | 4 | 40 | 1 | 1 | 0x008B6AC3@1 |
| StatusBitsUpgrade | 4 | 37 | 1 | 3 | 0x008B8A61@1 |
| PlayerHealSpecialPower | 3 | 222 | 1 | 2 | 0x008CC2CD@1 |
| StoreObjectsSpecialPower | 3 | 40 | 1 | 2 | 0x008D1E1E@1 |
| InvisibilitySpecialPower | 2 | 287 | 3 | 4 | 0x008C66B3@1 0x0081A857@3 0x007766C4@4 |
| ToggleHiddenSpecialAbilityUpdate | 2 | 194 | 1 | 12 | 0x008B1888@1 |
| AudioLoopUpgrade | 1 | 39 | 5 | 1 | 0x008554C5@0 0x008BBF4B@0 0x007B3CE0@1 0x0086232E@1 0x008BBD6A@1 |
| CastleUpgrade | 1 | 58 | 2 | 0 | 0x008BA912@1 0x007997E7@2 |
| CommandSetUpgrade | 1 | 80 | 2 | 3 | 0x008B7B65@1 0x008E3D1E@2 |
| CostModifierUpgrade | 1 | 44 | 13 | 0 | 0x008B9B0F@1 0x006AB681@2 0x006AFCE3@2 0x006B0060@2 0x006B2028@2 0x006AAD50@3 0x006B013B@3 0x006B1F55@3 |
| DominateEnemySpecialPower | 1 | 39 | 1 | 1 | 0x008D0E1D@1 |
| HeroModeSpecialAbilityUpdate | 1 | 98 | 1 | 4 | 0x0089583C@1 |
| ModelConditionUpgrade | 1 | 33 | 1 | 3 | 0x008BA557@1 |
| ProductionSpeedBonus | 1 | 45 | 6 | 1 | 0x008C726B@1 0x005FD6A5@2 0x006AEFBE@2 0x005FD431@3 0x006AE82A@3 0x0060D23A@4 |
| SpecialDisguiseUpdate | 1 | 196 | 3 | 12 | 0x008B42E6@0 0x008B4301@0 0x008B4271@1 |
| SummonReplacementSpecialAbilityUpdate | 1 | 328 | 3 | 14 | 0x008B1EE7@0 0x008B42E6@0 0x008B1E6A@1 |
| TransportContain | 1 | 239 | 2 | 10 | 0x00869E86@0 0x0086A3DB@0 |
| WeaponModeSpecialPowerUpdate | 1 | 117 | 2 | 4 | 0x00898357@0 0x0089822B@1 |

## Failing rows without a module link, by result

| result | rows |
|---|---:|
| NO-MODULE | 62 |
| LEFT-WORLD | 27 |
| FORMATION | 24 |
| NO-RULE | 19 |
| NO-EFFECT | 7 |
| NO-FIRE | 7 |
| NO-SEND | 7 |
| NOT-ON-BAR | 3 |
| STUCK | 2 |

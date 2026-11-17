# Port 268 — preliminary manual grouping check

The following is a check of an **intermediate** ranking, not a claim that the
final grouping is correct. Commands use `python tools/mod/rw.py
workspace/RotWK/game.dat dis <address> 12`. The binary is not included.

1. `ActiveBody`, S-1712/S-681/S-854: `RW 0x8B38A2` is an entry and
   `0x8B38B0` is inside it. The first two stops describe different damage
   paths, while S-854 concerns respawn. A shared class mention is not enough
   evidence of one task: split and examine how class mentions are attributed.
2. `ReplaceSelfUpgrade`, S-081/S-1986/S-233: `RW 0x5E586F` is a function
   entry, but it is locomotor numerics, whereas S-1986 is wall replacement.
   The reference-based split incorrectly leaves unrelated mechanisms together.
3. `RW 0x4BE587`, S-091/S-1580/S-5920: the function starts with an SEH
   prologue at `0x4BE587`; S-1580 and S-5920 cite interior instructions.
   Shared code is real, but parsing Lua scripts, animation timing, and a
   presentation adaptation still require separate scope review.
4. `SpecialPowerModule`, S-2006/S-2008: `RW 0x86C4EA` is a prologue and
   appears in both records, but the two sweep failures do not thereby prove a
   common mechanism. Naming the group after a mentioned class is too strong.
5. `SpecialAbilityUpdate`, S-7224/S-7360: `RW 0x85260A` starts with stack
   allocation and accesses the ability's sound state; S-7224 cites `0x85261D`.
   S-7360 describes building EVA. A shared function must not be conflated with
   proof that all these event paths are one implementation task.

Also corrected two false interior starts: `0x8A3F72` lies inside the map start
`0x8A3DD5`, and `0x4C36F8` lies inside `0x4C34A2` (both verified with
`rw.py dis`). The resolver now prefers a reachable map start over an
unverified interior candidate. Candidate entry detection still needs review;
only the cached whole-function inventory can supply its actual blocks.

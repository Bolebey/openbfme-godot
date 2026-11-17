# PORT-276: targetless trap / mine detonation

Fork issue 643, dashboard task 255. Target: verified RotWK 2.01 `game.dat`
(MD5 `daed3668006cd90f01c34e5a7da1901f`). All RW instructions below were
read with `tools/mod/rw.py dis`; BFME2 is a donor, not the target oracle.

## Scope and the first-check gate

The issue explicitly requires checking the existing engine first and stopping
if a target next to a detonating trap already takes damage. No engine fix is
justified until that check fails. The initial full `openbfme_tests` build is
running; `PORT-276*` uses synthetic INI, not an installed game or mod.

`WeaponFireSpecialAbilityUpdate` itself belongs to Local 76. Its functions
have not been changed. The generated function / field / matrix maps have not
been changed either.

## Effective mod data (read locally, not copied into Git)

The chain is **not** a special weapon directly damaging the enemy:

- `SalemePoisonTrap`, `SalemePoisonTrapStrong`, `SalemeBlindTrap`, and
  `SalemeExplodeTrap` use `SalemeDetonateFallen` to kill themselves with a
  BURNED death. The trigger affects allies and self and has SUICIDE and
  DamageDealtAtSelfPosition. The enemy-facing effect comes from the selected
  `SlowDeathBehavior` weapon, not from this trigger's damage estimate against
  an enemy.
- The poison and blind traps run an INITIAL death weapon; the explosive trap
  runs a FINAL death weapon. The blind trap's death weapon applies emotion
  and attribute-modifier nuggets, not an ordinary damage nugget.
- `IsengardDeployedExplosiveMine` uses `IsegardDetonateMine` with the same
  self-kill mechanism, followed by its BURNED FINAL death weapon.
- Their OCL powers also make auxiliary objects. That does not replace the
  special weapon -> self-kill -> death weapon chain examined here.

Sources inspected: `data/ini/object/evilfaction/units/harad/salemetraps.ini`,
`data/ini/object/evilfaction/units/isengard/mine.ini`, `data/ini/weapon.ini`,
`data/ini/includes/weapon.inc`, both object-creation-list files, and
`data/ini/specialpower.ini` in the effective Ennorath 1.9.8.3 archive.
No original INI text or copied numeric data is included here or in the tests.

## RotWK branch map and the existing port

| Entry (first instruction) | Branches read in RW | Existing port |
|---|---|---|
| RW 0x8961F7, `WeaponFireSpecialAbilityUpdate::triggerAbilityEffect` | 0x896220..0x896288: SuppressForHordes and horde-target redirection. 0x896288..0x89629D: weapon must exist and be ready. 0x8962A3..0x8962C0: set weapon owner ID; a victim uses the object overload. 0x8962D8..0x896313: without a victim, zero target position or NeedLivingTargets chooses `object + 0x38`, otherwise the stored position; call RW 0x6CF3D2. 0x896332..0x89636B: busy-until, base trigger, recharge. | `SpecialAbilityModules.cpp`, existing `triggerAbilityEffect`. No edits. |
| RW 0x6CF3D2, position force-fire wrapper | Pushes null victim and victim ID, the supplied position, ignoreRanges = true, special/projectile detonation = false, then calls RW 0x6CEF6D (0x6CF3D8..0x6CF3F0). This is the ordinary position-nugget path, not the projectile-detonation overload. | `ObjectWeapons::fireExtraWeaponAt`. |
| RW 0x6CEF6D, `Weapon::privateFireWeapon` | Bonus, template gate, optional assistance, leech deadline, ready gate (0x6CEF88..0x6CF010). Stream, rate / delay, ammo and barrel checks (0x6CF010..0x6CF092). Special branch (0x6CF092), linear-target branch (0x6CF0AE), scatter-list branch (0x6CF173), otherwise pass the position unchanged to RW 0x6CC915 (0x6CF259..0x6CF27B). Shot counters, ammo, barrel, reload / delay tail follow delivery (0x6CF280..0x6CF325). No alive-source veto here. | `Object/WeaponState.cpp`, `privateFireWeapon`. |
| RW 0x6CC915, `WeaponTemplate::fireWeaponTemplate` | Null source or both victim and position absent returns (0x6CC920..0x6CC93E). The supplied position is valid without a victim. Ignore-ranges or unexpired leech deadline skips range checks (0x6CCA2C..0x6CCAA1). Fire FX precede damage. DamageDealtAtSelfPosition selects the source position and clears the victim (0x6CCBF9..0x6CCC10); the GiantBird AI case keeps the target z (0x6CCC38..0x6CCC51). Miss / dodge / passenger handling and ScatterIndependently precede the nugget loop. A victim uses nugget slots 1 / 5 (0x6CCED3..0x6CCEF5); no victim uses slots 2 / 6 (0x6CCEFA..0x6CCF17). | `ObjectWeaponDelivery::fireWeaponTemplate`, existing self-position and radius branches. |
| RW 0x90DEF0, `DamageNugget` position effect | Radius is clamped to at least one (0x90DF0C..0x90DF1F). Source is looked up by weapon owner ID (0x90DF24..0x90DF30); dying is not the same as absent. 0x90DF35..0x90DF77 selects cylinder / bounding sphere / geometry overlap. Partition query at 0x90DFB9. For each object: arc test, applicability slot 1 (0x90E199..0x90E1A6), height caps, minimum radius, apply slot 14 (0x90E23A..0x90E243); the splash threshold may stop after the first successful hit. | `WeaponDelivery.cpp`, `radiusDamage`, `damageAreaDistance`. |
| RW 0x90D77C / RW 0x90E855, nugget applicability | Upgrade / self / similar / projectile / height / relationship / filter gates are in the base. `SELF` admits the source (0x90D7CA..0x90D7F6). Damage applicability fills a no-flank record, requires a victim and a findable source (0x90E874..0x90E8A8), checks leadership, estimates damage, and accepts positive damage **or a forced kill** (0x90E994..0x90E9A6). No effectively-dead-source gate. | Existing `shouldDeliver` and `damageApplicable`. Other unrelated base-nugget branches are outside this task. |
| RW 0x90E28C, `DamageNugget` fill | At 0x90E603..0x90E61A compare weapon source ID with victim ID, test RadiusDamageAffects bit 0x10 (SUICIDE), and set `DamageInfo + 0x24` (kill). The nugget's death type is preserved (0x90E641..0x90E647). Ordinary damage without SUICIDE does not force a kill. | `Object/Damage.cpp`, existing `FillDamageInfo`. |
| RW 0x90E683 / RW 0x698E7D, apply damage | Fill the record (0x90E6A9), HEALING takes its separate path, otherwise `Object::attemptDamage` (0x90E6C7). RW 0x698E7D defers one frame only when the **victim** has UPDATING_AI, or when the record has a positive delay; otherwise it applies immediately (0x698E7D..0x698EBA). | Existing `applyToVictim` and `Object::attemptDamage`. |
| RW 0x861712 / RW 0x860E93, slow-death selection / beginning | `SlowDeathBehavior` selects the applicable death mux and begins it. INITIAL is dispatched at the end of `beginSlowDeath`; it is not cancelled because the source has just died. | `CombatModules.cpp`, existing `onDie` / `beginSlowDeath`. |
| RW 0x8609A8, phase effect | FX, OCL, weapon, sound lists, in order. The phase weapon is selected with logic RNG; non-null weapon (0x860A4B..0x860A7B) calls RW 0x6CF530 with the dying object and `object + 0x38` (0x860A7D..0x860A8C). | Existing `SlowDeathBehavior::doPhase`. |
| RW 0x6CF530, temporary position weapon | Allocate primary-slot weapon, set owner ID, load ammo, set leech deadline to frame + 1, fire at the supplied position, release weapon (0x6CF53F..0x6CF582). No dead-source gate. | Existing `ObjectWeapons::createAndFireTempWeapon` / `fireTempWeapon`. |
| RW 0x860B39, slow-death update | On the destruction frame, dispatch FINAL (phase 2) **before** destroying the source: 0x860E50 `push 2`; 0x860E55 `call 0x8609A8`; 0x860E61 `call 0x62BBAB`. Thus the temporary weapon can still resolve its source while applying the blast. | Existing `SlowDeathBehavior::update`, same order. |

The map's unported class slots RW 0x896156 / RW 0x896370 are Local 76's
range / update work, not evidence that the existing position weapon cannot
deal damage. The generated map labels RW 0x8961F7 as ported already.

### BFME2 donor cross-check

- DONOR BFME2 0x00492A72,
  `GameEngine/Source/GameLogic/Object/Update/WeaponFireSpecialAbilityUpdate.cpp`:
  no victim chooses stored position or the source, then `forceFireWeapon`.
- DONOR BFME2 0x002CE76C,
  `GameEngine/Source/GameLogic/Object/WeaponPrivateFireWrappers_O1.cpp`:
  position `forceFireWeapon` passes ignoreRanges and no victim.
- DONOR BFME2 0x002CE30A,
  `GameEngine/Source/GameLogic/Object/WeaponPrivateFireWeapon.cpp`:
  regular / special / linear / scatter dispatch, then timing and ammo tail.
- DONOR BFME2 0x00507D84,
  `GameEngine/Source/GameLogic/Object/DamageNugget.cpp`:
  position effect queries neighbours, then applies each eligible nugget.
- DONOR BFME2 0x0045D97A / 0x0045DB0B,
  `GameEngine/Source/GameLogic/Object/Behavior/SlowDeathBehaviorUpdate.cpp`:
  temporary death weapon at the source's position; FINAL before destruction.

SUICIDE is independently confirmed in RW 0x90E28C, not assumed from the
BFME2 damage-info donor (which differs here).

## Diagnostic tests and outstanding check

`engine/tests/test_port_276.cpp`, included by `engine/cmake/Port276.cmake`
in the normal full test target:

1. Targetless OCL special power and WeaponFire update, no normal weapon set:
   self-kill with BURNED death, INITIAL and FINAL blast variants, exact
   damage to an enemy inside the radius, no damage outside or to an ally,
   source gone afterward and blast attributed to its ID.
2. Neighbour: remove SUICIDE, keep SELF and self-position delivery; a single
   ordinary wound to the source, no death weapon and no enemy damage.

These tests intentionally run **before any engine modification**. Final
build and test output, and whether the issue's first-check gate is reached,
will be recorded after the complete `openbfme_tests` target is built.

INFERENCE: the matrix may be asking the allies-only trigger for enemy damage
rather than observing its death weapon (especially the blind trap's non-damage
effect). This is a hypothesis from `SweepEffectProbe.h`, not a reason to
change the engine or the probe in this issue. No probe code has been edited.

Осталось: завершить полный build и прогон `-tc="PORT-276*"` на исходном
движке; при зелёном воспроизведении выполнить прямое указание issue:
остановиться и сообщить результат владельцу, не изобретая правку.

// OpenBFME. GPL-3.0.
// PORT-276: targetless detonation, including the dying firer's INITIAL / FINAL weapon.
// Synthetic INI only. The trigger is a self-kill, not the enemy-facing blast:
// RW 0x8961F7 (0x8962D8..0x896313) -> RW 0x6CF3D2 -> RW 0x6CEF6D -> RW 0x6CC915;
// RW 0x90E28C (0x90E603..0x90E61A) sets kill for SUICIDE against the source itself.
// The death weapon is fired at a position before destruction (RW 0x8609A8 / RW 0x860B39).

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"

#include <string>

namespace
{
const char kPort276Ini[] = R"ini(
SpecialPower P276Detonation
 Enum = SPECIAL_GENERAL_TARGETLESS
 ReloadTime = 60000
End
Weapon P276Igniter
 AttackRange = 500
 DelayBetweenShots = 0
 PreAttackDelay = 0
 FiringDuration = 0
 RadiusDamageAffects = ALLIES SELF SUICIDE
 DamageDealtAtSelfPosition = Yes
 DamageNugget
  Damage = 3
  Radius = 12
  DamageType = SLASH
  DeathType = BURNED
 End
End
Weapon P276Explosion
 AttackRange = 4
 RadiusDamageAffects = ENEMIES NEUTRALS SELF
 DamageNugget
  Damage = 37
  Radius = 48
  DamageType = FLAME
  DeathType = BURNED
 End
End
Object P276Trap
 KindOf = SELECTABLE IMMOBILE MINE DEPLOYED_MINE
 Geometry = SPHERE
 GeometryMajorRadius = 2
 Body = ActiveBody BodyTag
  MaxHealth = 120
 End
 Behavior = AIUpdateInterface AiTag
  AutoAcquireEnemiesWhenIdle = No
 End
 Behavior = PhysicsBehavior PhysicsTag
 End
 Behavior = SlowDeathBehavior ExplosionTag
  DeathTypes = NONE +BURNED
  DestructionDelay = 400
  Weapon = INITIAL P276Explosion
 End
 Behavior = SlowDeathBehavior OtherDeathTag
  DeathTypes = ALL -BURNED
  DestructionDelay = 400
 End
 Behavior = OCLSpecialPower PowerTag
  SpecialPowerTemplate = P276Detonation
  CreateLocation = CREATE_AT_LOCATION
 End
 Behavior = WeaponFireSpecialAbilityUpdate AbilityTag
  SpecialPowerTemplate = P276Detonation
  SpecialWeapon = P276Igniter
  WhichSpecialWeapon = 1
  UnpackTime = 600
  MustFinishAbility = Yes
  AwardXPForTriggering = 0
 End
End
Object P276Victim
 KindOf = INFANTRY IMMOBILE
 Geometry = SPHERE
 GeometryMajorRadius = 1
 Body = ActiveBody BodyTag
  MaxHealth = 200
 End
End
)ini";

struct DetonationWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;

	explicit DetonationWorld(bool finalWeapon = false, bool suicide = true)
	{
		logic->setEdition(GameEdition::RotWK201);
		combat().setAutoAcquireEnabled(false);
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		std::string data(kPort276Ini);
		if (finalWeapon)
		{
			const std::string initial = "Weapon = INITIAL P276Explosion";
			data.replace(data.find(initial), initial.size(), "Weapon = FINAL P276Explosion");
		}
		if (!suicide)
		{
			const std::string mask = "ALLIES SELF SUICIDE";
			data.replace(data.find(mask), mask.size(), "ALLIES SELF");
		}
		const std::string error = w.load(data, INI_LOAD_OVERWRITE, "port276.ini");
		REQUIRE_MESSAGE(error.empty(), error);
	}

	~DetonationWorld()
	{
		logic->reset();
		TheSpecialPowerStore = savedPowers;
	}

	bool detonate(Object &trap)
	{
		const SpecialPowerTemplate *power = powers.findSpecialPowerTemplate("P276Detonation");
		REQUIRE(power);
		return SpecialPowerModules::doSpecialPower(trap, power, 0, false);
	}
};
} // namespace

TEST_CASE("PORT-276 targetless self-kill fires the trap's INITIAL or FINAL radius weapon before destroying the trap")
{
	for (bool finalWeapon : { false, true })
	{
		CAPTURE(finalWeapon);
		DetonationWorld world(finalWeapon);
		Object *trap = world.unit("P276Trap", 'A', 300, 300);
		Object *inside = world.unit("P276Victim", 'B', 330, 300);
		Object *outside = world.unit("P276Victim", 'B', 400, 300);
		Object *ally = world.unit("P276Victim", 'A', 330, 310);
		REQUIRE(trap);
		REQUIRE(inside);
		REQUIRE(outside);
		REQUIRE(ally);
		const ObjectID trapId = trap->getID();
		world.frames(2);
		CHECK(world.health(inside) == 200);
		CHECK(world.health(outside) == 200);
		CHECK(world.health(ally) == 200);
		REQUIRE(world.detonate(*trap)); // no victim and no location argument
		world.frames(20);

		// RW 0x6CC915 (0x6CCBFF..0x6CCC51) uses the firer's position, not (0,0,0),
		// then calls each nugget's position slot (0x6CCEFA..0x6CCF17).
		// RW 0x90DEF0 queries the radius; SUICIDE's BURNED death selects the explosion.
		// Exactly one 37-point blast: neither the 3-point allies-only igniter nor two shots.
		CHECK(world.health(inside) == 163);
		CHECK(world.health(outside) == 200);
		CHECK(world.health(ally) == 200);
		CHECK(world.byId(trapId) == nullptr); // never dereference a trap after its death
		const auto *body = dynamic_cast<const ActiveBody *>(inside->getBodyModule());
		REQUIRE(body);
		CHECK(body->lastDamager() == trapId); // the dead firer, not a replacement object
	}
}

TEST_CASE("PORT-276 neighbouring self-position weapon without SUICIDE only wounds the firer and does not fire a death weapon")
{
	DetonationWorld world(false, false);
	Object *trap = world.unit("P276Trap", 'A', 300, 300);
	Object *enemy = world.unit("P276Victim", 'B', 306, 300); // inside the igniter's radius, but not its relationship mask
	REQUIRE(trap);
	REQUIRE(enemy);
	const ObjectID trapId = trap->getID();
	world.frames(2);
	REQUIRE(world.detonate(*trap));
	world.frames(20);

	// RW 0x90E28C (0x90E603..0x90E61A): without bit 0x10, ordinary damage, no forced kill.
	REQUIRE(world.byId(trapId));
	CHECK_FALSE(world.byId(trapId)->isEffectivelyDead());
	CHECK(world.health(world.byId(trapId)) == 117);
	CHECK(world.health(enemy) == 200);
	const auto *ability = dynamic_cast<const WeaponFireSpecialAbilityUpdate *>(world.byId(trapId)->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(ability);
	CHECK(ability->shotsFired() == 1);
}

// OpenBFME unit tests. PORT-136: synthetic, no game installation. GPL-3.0.
// The summoned giant bird of Ennorath's spell book (issue #338) must attack on its own. Its attack is the bird's swoop-then-idle
// machine (RW 0x769C31 -> RW 0x769335), which the attack state now asks its AI for before it makes the ground machine: that
// machine's move is the walk machine (RW 0x6668CA -> the pathfinder's ground search RW 0x6FC18E), which never gives a flyer a
// path, so the eagle used to find its enemy, enter the attack states and sit at its spawn point.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "IniTestUtil.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/Object.h"

namespace
{
// the real mod shape of GwaihirEagleSpell_Summoned (Ennorath 1.9.8.2): its KindOf list, VisionRange 450, AttackRange 20, the two DamageNuggets (FORCE,
// HERO), a MetaImpactNugget, the TransportContain grab and LifetimeUpdate; `extras` adds the two modules beside GiantBirdAIUpdate.
std::string eagleIni(bool extras)
{
	std::string s = R"ini(
Locomotor Port136BirdLoco
 Surfaces = AIR
 Appearance = GIANT_BIRD
 TurnTime = 1000
 Acceleration = 1000
 Braking = 1000
 PreferredHeight = 100
 ZAxisBehavior = SURFACE_RELATIVE_HEIGHT
End
Weapon Port136EagleAttack
 AttackRange = 20.0
 DelayBetweenShots = 4000
 AntiAirborneVehicle = Yes
 AntiAirborneMonster = Yes
 AntiAirborneInfantry = Yes
 RadiusDamageAffects = ENEMIES ALLIES SELF
 DamageNugget
  Damage = 50
  DamageType = FORCE
  DeathType = NORMAL
  Radius = 0
 End
End
Object Port136Eagle
 KindOf = PRELOAD CAN_ATTACK CAN_CAST_REFLECTIONS PASS_EXPERIENCE_TO_PRODUCER SCORE NO_FREEWILL_ENTER MONSTER SCARY CANNOT_RETALIATE SUMMONED
 VisionRange = 450
 ShroudClearingRange = 650
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 30
 WeaponSet
  Conditions = None
  Weapon = PRIMARY Port136EagleAttack
 End
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 5000
 End
 Behavior = GiantBirdAIUpdate ModuleTag_AI
  AutoAcquireEnemiesWhenIdle = Yes ATTACK_BUILDINGS
 End
)ini";
	if (extras)
	{
		s += R"ini( Behavior = StancesBehavior ModuleTag_Stances
  StanceTemplate = Port136Hero
 End
)ini";
		s += R"ini( Behavior = TransportContain ModuleTag_Contain
  Slots = 1
  PassengerBonePrefix = PassengerBone:B_Passenger KindOf:GRAB_AND_DROP
  DamagePercentToUnits = 0%
 End
)ini";
		// probe N: a LifetimeUpdate with MinLifetime/MaxLifetime 120000 kills the eagle at ~500 frames, so the fixture leaves it out (a guard test must run long)
	}
	s += R"ini( LocomotorSet
  Locomotor = Port136BirdLoco
  Condition = SET_NORMAL
  Speed = 120
 End
End
StanceTemplate Port136Hero
    Stance Aggressive
        MeleeBehavior = Amoeba
        End
    End
    Stance Battle
        MeleeBehavior = Swarm
        End
    End
    Stance HoldGround
        MeleeBehavior = HoldGround
        End
    End
End
Armor Port136EagleArmor
 Armor = DEFAULT 100%
 Armor = HERO 100%
 Armor = FORCE 100%
End
)ini";
	return s;
}
// the same foot object, but with a StancesBehavior: the fresh stance is Battle, so it guards like the eagle (RW 0x8622E9 -> 0x662A21)
const char *kFootStanceIni = R"ini(
Object Port136FootStance
 KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE
 VisionRange = 450
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 ArmorSet
  Conditions = None
  Armor = PlainArmor
 End
 WeaponSet
  Conditions = None
  Weapon = PRIMARY Port136EagleAttack
 End
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
 Behavior = AIUpdateInterface ModuleTag_AI
  AutoAcquireEnemiesWhenIdle = Yes
  MoodAttackCheckRate = 400
 End
 Behavior = StancesBehavior ModuleTag_Stances
  StanceTemplate = Port136Hero
 End
 Behavior = PhysicsBehavior ModuleTag_Physics
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 55
 End
End
)ini";
} // namespace

// the mod shape of GwaihirEagleSpell_Summoned (Ennorath 1.9.8.2) with its StancesBehavior: it spawns in the guard state
TEST_CASE("PORT-136: the eagle in the guard state takes off, closes in and hits its target")
{
	combattest::CombatWorld w;
	StanceTemplateStore store;
	store.registerBlock(w.w.fx.env.blocks);
	StanceTemplateStore *const saved = TheStanceTemplateStore;
	TheStanceTemplateStore = &store;
	struct Restore
	{
		StanceTemplateStore *v;
		~Restore() { TheStanceTemplateStore = v; }
	} restore{saved};
	const std::string error = w.w.load(eagleIni(true), INI_LOAD_OVERWRITE, "port136-guard.ini");
	REQUIRE_MESSAGE(error.empty(), error);
	w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	Object *bird = w.unit("Port136Eagle", 'A', 200.0f, 200.0f);
	Object *enemy = w.unit("Swordsman", 'B', 350.0f, 200.0f);
	REQUIRE(bird);
	REQUIRE(enemy);
	auto *ai = dynamic_cast<GiantBirdAIUpdate *>(bird->getAIUpdateInterface());
	REQUIRE(ai);
	REQUIRE(StancesBehavior::of(*bird));
	// the machine the attack state asks its AI for is the bird's swoop machine (RW 0x769C31 -> RW 0x769335), not a ground machine
	CHECK(ai->makeAttackThenIdleMachine(10) != nullptr);
	const Coord3D from = *bird->getPosition();
	const float startHealth = w.health(enemy);
	const int hit = w.runUntil([&] { return w.health(enemy) < startHealth; }, 1500);
	MESSAGE("PORT-136: hit after " << hit << " frames, bird at (" << bird->getPosition()->x << "," << bird->getPosition()->y << "," << bird->getPosition()->z << ") state=" << ai->currentStateId() << " enemy hp "
				   << startHealth << " -> " << w.health(enemy));
	REQUIRE(hit < 1500);
	// it flew there: a bird 150 units out never walks, and the guard's approach only ever had the walk machine's path
	CHECK(bird->getPosition()->x > from.x + 50.0f);
	CHECK(bird->getPosition()->z > 1.0f);
	const int killed = w.runUntil([&] { return w.health(enemy) <= 0.0f; }, 4000);
	MESSAGE("PORT-136: enemy dead after " << (hit + killed) << " frames");
	CHECK(killed < 4000);
}

// the neighbour: a foot unit of the same mod (StancesBehavior, the fresh Battle stance guards like the eagle) walks and kills as
// before — the swoop machine is the bird's alone, every other AI answers null to the factory and keeps its ground machine
TEST_CASE("PORT-136: a foot unit in the guard state still walks to its target and hits it")
{
	combattest::CombatWorld w;
	StanceTemplateStore store;
	store.registerBlock(w.w.fx.env.blocks);
	StanceTemplateStore *const saved = TheStanceTemplateStore;
	TheStanceTemplateStore = &store;
	struct Restore
	{
		StanceTemplateStore *v;
		~Restore() { TheStanceTemplateStore = v; }
	} restore{saved};
	const std::string error = w.w.load(eagleIni(true) + kFootStanceIni, INI_LOAD_OVERWRITE, "port136-guard-foot.ini");
	REQUIRE_MESSAGE(error.empty(), error);
	w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	Object *foot = w.unit("Port136FootStance", 'A', 200.0f, 600.0f);
	Object *enemy = w.unit("Swordsman", 'B', 350.0f, 600.0f);
	REQUIRE(foot);
	REQUIRE(enemy);
	auto *ai = dynamic_cast<AIUpdateInterface *>(foot->getAIUpdateInterface());
	REQUIRE(ai);
	REQUIRE(StancesBehavior::of(*foot));
	CHECK(ai->makeAttackThenIdleMachine(10) == nullptr); // no bird factory: the ground machine is made as before
	const float startHealth = w.health(enemy);
	const int hit = w.runUntil([&] { return w.health(enemy) < startHealth; }, 1500);
	MESSAGE("PORT-136 foot: hit after " << hit << " frames, foot at (" << foot->getPosition()->x << "," << foot->getPosition()->y << ") state=" << ai->currentStateId() << " enemy hp " << startHealth << " -> "
				   << w.health(enemy));
	REQUIRE(hit < 1500);
	CHECK(foot->getPosition()->x > 200.0f + 20.0f); // it walked
	CHECK(foot->getPosition()->z <= 1.0f);
}


// PORT-277: first check the existing main path before changing the engine.
// Synthetic INI only. RW 0x911A58: a horde locks LockWeaponSlot temporarily,
// then orders members through HordeContainInterface vtable +0x04 (RW 0x875221).
// A single firer without a contain returns without an attack (RW 0x911A58, contain check at +0x1E).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

using namespace combattest;
namespace
{
const char kHit[] = R"ini(
Weapon P277Hit
AttackRange = 400
DelayBetweenShots = 10000
DamageNugget
	Damage = 7
	DamageType = MAGIC
	DeathType = NORMAL
End
End
)ini";
const char kPort277[] =
	"Weapon P277Rangefinder\n"
	"  AttackRange = 150\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  HordeAttackNugget\n"
	"    LockWeaponSlot = SECONDARY\n"
	"  End\n"
	"End\n"
	"Object P277Archer\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 250\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY BowWeapon\n"
	"    Weapon = SECONDARY P277Hit\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object P277ArcherHorde\n"
	"  KindOf = HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 250\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY P277Rangefinder\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:P277Archer Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:P277Archer Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = P277Archer 6\n"
	"    RanksToReleaseWhenAttacking = 1 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("PORT-277: existing rangefinder orders members to fire SECONDARY and damage the target")
{
	std::string ini = std::string(kHit) + kPort277;
	SUBCASE("member weapon has a direct DamageNugget") {}
	SUBCASE("member weapon has only a ProjectileNugget; damage is in its warhead")
	{
		const std::string assignment = "Weapon = SECONDARY P277Hit";
		const auto at = ini.find(assignment);
		REQUIRE(at != std::string::npos);
		ini.replace(at, assignment.size(), "Weapon = SECONDARY Arc20");
	}
	CombatWorld w(ini.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P277ArcherHorde", 'A', 300, 300);
	Object *target = w.unit("Dummy", 'B', 430, 300);
	w.frames(12);
	const auto members = w.membersOf(horde);
	REQUIRE(members.size() == 6);
	const auto *rangefinder = w.stores.weapons().findWeaponTemplate("P277Rangefinder");
	REQUIRE(rangefinder != nullptr);
	const float health = w.health(target);
	// No ordinary attack order to the horde: only the ability's extra weapon fires.
	ObjectWeapons::createAndFireTempWeaponAt(rangefinder, horde, *target);
	CHECK(w.hordeOf(horde)->attackStats().orders == 6);
	CHECK(w.health(target) == health); // the rangefinder has no damage of its own
	for (Object *m : members)
	{
		REQUIRE(m->getWeapons() != nullptr);
		CHECK(m->getWeapons()->curSlot() == 1);
		CHECK(m->getWeapons()->isCurWeaponLocked());
	}
	// tolerance: timeout only; direct hits and projectile flight have different arrival frames.
	CHECK(w.runUntil([&] { return w.health(target) < health; }, 100) < 100);
	// tolerance: stop at the first damaging frame; one or more of six shots can arrive together.
	CHECK(w.health(target) <= health - 7.0f);
}

TEST_CASE("PORT-277: single firer with no HordeContain does not turn the rangefinder into a damaging weapon")
{
	const std::string ini = std::string(kHit) + kPort277;
	CombatWorld w(ini.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *single = w.unit("P277Archer", 'A', 300, 300);
	Object *target = w.unit("Dummy", 'B', 430, 300);
	w.frames(12);
	REQUIRE(single->getContain() == nullptr);
	const auto *rangefinder = w.stores.weapons().findWeaponTemplate("P277Rangefinder");
	REQUIRE(rangefinder != nullptr);
	const float health = w.health(target);
	ObjectWeapons::createAndFireTempWeaponAt(rangefinder, single, *target);
	w.frames(20);
	CHECK(w.health(target) == health);
	CHECK(single->getWeapons()->curSlot() == 0);
	CHECK_FALSE(single->getWeapons()->isCurWeaponLocked());
}

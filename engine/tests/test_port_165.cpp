// OpenBFME. GPL-3.0.
// PORT-165 synthetic regression: a MELEE_HORDE changes to a ranged position weapon.
// The position-attack machine is selected at RW 0x74CFF1..0x74D019.
#include "doctest.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "CombatTestUtil.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackHordePosition.h"
#include "GameLogic/AI/WeaponCommands.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/WeaponSetToggle.h"

using namespace combattest;
namespace {
const char kSynthetic[] =
	"Weapon P165HordeRangefinder\n"
	"  AttackRange = 12\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  HordeAttackNugget\n"
	"  End\n"
	"End\n"
	"Weapon P165HordeBombardRangefinder\n"
	"  AttackRange = 250\n"
	"  MinimumAttackRange = 50\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  AcceptableAimDelta = 45\n"
	"  NoVictimNeeded = Yes\n"
	"  BombardType = Yes\n"
	"  ScatterRadius = 40\n"
	"  HordeAttackNugget\n"
	"  End\n"
	"End\n"
	"Weapon P165Bow\n"
	"  AttackRange = 300\n"
	"  DelayBetweenShots = 0\n"
	"  ClipSize = 1\n"
	"  AutoReloadsClip = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon P165BowBombard\n"
	"  AttackRange = 350\n"
	"  MinimumAttackRange = 25\n"
	"  DelayBetweenShots = 800\n"
	"  ClipSize = 1\n"
	"  AutoReloadsClip = Yes\n"
	"  NoVictimNeeded = Yes\n"
	"  BombardType = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Object P165Archer\n"
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
	"    Weapon = PRIMARY P165Bow\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = WEAPONSET_TOGGLE_1\n"
	"    Weapon = PRIMARY P165Bow\n"
	"    Weapon = TERTIARY P165BowBombard\n"
	"    AutoChooseSources = PRIMARY FROM_PLAYER FROM_SCRIPT FROM_AI\n"
	"    AutoChooseSources = TERTIARY FROM_PLAYER FROM_SCRIPT\n"
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
	"Object P165ArcherHorde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
		"  VisionRange = 250\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY P165HordeRangefinder\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = WEAPONSET_TOGGLE_1\n"
	"    Weapon = PRIMARY P165HordeRangefinder\n"
	"    Weapon = TERTIARY P165HordeBombardRangefinder\n"
	"    AutoChooseSources = TERTIARY FROM_PLAYER FROM_SCRIPT\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:P165Archer Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:P165Archer Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = P165Archer 6\n"
	"    RanksToReleaseWhenAttacking = 1 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

struct TestWorld : CombatWorld
{
	TestWorld() : CombatWorld(kSynthetic) { WeaponCommands::registerHandlers(dispatcher); }
};

}

TEST_CASE("PORT-165: position horde wait tracks active member states (RW 0x747999, 0x742CEA, 0x86D562)")
{
	TestWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
	REQUIRE(horde);
	w.frames(12);
	const auto members = w.membersOf(horde);
	REQUIRE(members.size() == 6);
	AIStateMachine hordeMachine(*horde->getAIUpdateInterface(), "P165WaitTest");
	AIHordePositionWaitState wait(hordeMachine);
	CHECK(wait.onEnter() == STATE_CONTINUE);
	CHECK(wait.update() == STATE_FAILURE); // at least one member is not active
	AIStateMachine memberMachine(*members.front()->getAIUpdateInterface(), "P165NonHordeWaitTest");
	AIHordePositionWaitState nonHordeWait(memberMachine);
	CHECK(nonHordeWait.onEnter() == STATE_FAILURE);
	const Coord3D spot{300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f)};
	for (Object *member : members)
	{
		WeaponSetToggle::setObjectWeaponSetFlag(*member, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
		member->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY);
		member->getAIUpdateInterface()->aiAttackPosition(spot, 0x7fffffff, CMD_FROM_AI);
	}
	CHECK(wait.update() == STATE_CONTINUE); // every live member's state reports active
	Object *target = w.unit("Dummy", 'B', 500, 500);
	REQUIRE(target);
	hordeMachine.setGoalObject(target->getID());
	for (Object *member : members) member->getAIUpdateInterface()->setCurrentVictim(target);
	CHECK(wait.update() == STATE_CONTINUE); // RW 0x86D438: all have the same victim
	members.front()->getAIUpdateInterface()->setCurrentVictim(nullptr);
	CHECK(wait.update() == STATE_FAILURE);
	hordeMachine.setGoalObject((ObjectID)INVALID_ID);
	members.front()->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	CHECK(wait.update() == STATE_FAILURE); // one inactive member is sufficient
}

namespace {
struct PositionFireNotify : AttackFireNotify
{
	int shots = 0;
	bool slotAllowed = true;
	void notifyFired() override { ++shots; }
	void notifyNewVictimChosen(Object *) override {}
	bool isWeaponSlotOkToFire(int) const override { return slotAllowed; }
	bool isAttackingObject() const override { return false; }
};
}

TEST_CASE("PORT-165: position-fire state tests position range and fires once (RW 0x74C911 / 0x74C9FF)")
{
	TestWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
	REQUIRE(horde);
	w.frames(12);
	WeaponSetToggle::setObjectWeaponSetFlag(*horde, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
	horde->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY);
	AIStateMachine machine(*horde->getAIUpdateInterface(), "P165PositionFire");
	PositionFireNotify notify;
	AIHordePositionFireState fire(machine, notify);
	machine.setGoalPosition(Coord3D{300.0f, 900.0f, 0.0f});
	CHECK(fire.onEnter() == STATE_FAILURE);
	machine.setGoalPosition(Coord3D{300.0f, 150.0f, 0.0f});
	REQUIRE(fire.onEnter() == STATE_CONTINUE);
	notify.slotAllowed = false;
	CHECK(fire.update() == STATE_FAILURE);
	CHECK(notify.shots == 0);
	notify.slotAllowed = true;
	CHECK(fire.update() == STATE_SUCCESS);
	CHECK(notify.shots == 1);
	CHECK(horde->getWeapons()->stats().shotsFired == 1);
	fire.onExit(EXIT_NORMAL);
	CHECK(fire.onEnter() == STATE_SUCCESS); // weapon is not yet ready
}

TEST_CASE("PORT-165: deferred position-fire state survives save/load (RW 0x74C9C5 / 0x74CA35)")
{
	TestWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
	REQUIRE(horde);
	w.frames(12);
	const auto members = w.membersOf(horde);
	REQUIRE(!members.empty());
	Object *member = members.front();
	WeaponSetToggle::setObjectWeaponSetFlag(*member, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
	member->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY);
	if ((member->getAIUpdateInterface()->frame() & 1u) == 0) w.frames(1);
	REQUIRE((member->getAIUpdateInterface()->frame() & 1u) == 1u);
	AIStateMachine machine(*member->getAIUpdateInterface(), "P165DeferredFire");
	machine.setGoalPosition(Coord3D{300.0f, 150.0f, 0.0f});
	PositionFireNotify notify;
	AIHordePositionFireState fire(machine, notify);
	REQUIRE(fire.onEnter() == STATE_CONTINUE);
	XferSave save;
	fire.xfer(save);
	AIHordePositionFireState restored(machine, notify);
	XferLoad load(save.data());
	restored.xfer(load);
	CHECK(restored.update() == STATE_CONTINUE); // deferred pre-fire, no shot on this update
	CHECK(notify.shots == 0);
	CHECK(restored.update() == STATE_SUCCESS);
	CHECK(notify.shots == 1);
	restored.onExit(EXIT_NORMAL);
}

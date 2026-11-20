// OpenBFME. GPL-3.0.
// LOCAL-96 (fork issue 761): DozerAIUpdate and WorkerAIUpdate own builder functions from RotWK 2.01 game.dat
// (md5 daed3668006cd90f01c34e5a7da1901f):
//   * DozerAIUpdate::onOwnerDie (RW 0x0088BF7A, main vslot +0x24C, called from Object::onDie RW 0x00698FB5):
//     removes an unplaced phantom structure and refunds its cost immediately on builder death (P-157).
#include "doctest.h"

#include "CombatTestUtil.h"
#include "ProdTestUtil.h"

#include "Common/Player.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/WorkerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <string>

namespace
{
const char kLocal96Ini[] =
	"Object L96Porter\n"
	"  KindOf = INFANTRY SELECTABLE DOZER\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = DozerAIUpdate ModuleTag_AI\n"
	"    RepairHealthPercentPerSecond = 10%\n"
	"  End\n"
	"  Behavior = SlowDeathBehavior ModuleTag_Death\n"
	"    DeathTypes = ALL\n"
	"    DestructionDelay = 30000\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"End\n"
	"Object L96Worker\n"
	"  KindOf = INFANTRY SELECTABLE DOZER SWARM_DOZER\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = WorkerAIUpdate ModuleTag_AI\n"
	"    MaxBoxes = 1\n"
	"    RepairHealthPercentPerSecond = 10%\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"End\n"
	"Object L96Structure\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  BuildCost = 300\n"
	"  BuildTime = 10.0\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 400\n"
	"  End\n"
	"  Behavior = GettingBuiltBehavior ModuleTag_Build\n"
	"    RebuildTimeSeconds = 10\n"
	"  End\n"
	"End\n";

struct Local96World : combattest::CombatWorld
{
	Local96World() : CombatWorld(kLocal96Ini)
	{
		logic->productionSettings() = prodtest::defaultSettings();
		logic->settings().buildRulesLoaded = true;
		playerOf('A')->depositMoney(1000, false);
		frames(3);
	}

	Object *spawnAt(const char *name, float x, float y)
	{
		Object *o = spawn(name, x, y, 0.0f, teamA());
		REQUIRE(o != nullptr);
		return o;
	}
};
} // namespace

TEST_CASE("LOCAL-96 P-157: DozerAIUpdate::onOwnerDie (RW 0x0088BF7A) removes unplaced phantom and refunds cost on builder death")
{
	Local96World w;
	Object *porter = w.spawnAt("L96Porter", 200.0f, 200.0f);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	REQUIRE(dozer != nullptr);

	const ThingTemplate *bldgTmpl = w.w.things.findTemplate("L96Structure");
	REQUIRE(bldgTmpl != nullptr);

	const std::uint32_t moneyBefore = (std::uint32_t)w.cash('A');
	const Coord3D site{ 500.0f, 500.0f, 0.0f }; // far enough that the porter has not reached it yet
	Object *phantom = dozer->construct(*bldgTmpl, site, 0.0f, *w.playerOf('A'), false);
	REQUIRE(phantom != nullptr);
	const ObjectID phantomId = phantom->getID();
	REQUIRE(dozer->unplacedStructure() == phantomId);
	REQUIRE((std::uint32_t)w.cash('A') == moneyBefore - 300);

	// Kill the porter mid-march while SlowDeathBehavior keeps its corpse alive for 30s (DestructionDelay = 30000).
	porter->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	// RW 0x00698FB5 -> RW 0x0088BF7A removes the phantom immediately in the death frame, before SlowDeathBehavior deletes the corpse.
	CHECK(porter->isEffectivelyDead());
	CHECK_FALSE(porter->isDestroyed());
	CHECK(dozer->unplacedStructure() == INVALID_ID);
	CHECK((std::uint32_t)w.cash('A') == moneyBefore);
	w.frames(1);
	CHECK(w.logic->findObjectByID(phantomId) == nullptr);
}

TEST_CASE("LOCAL-96 P-157 neighbour: DozerAIUpdate::onOwnerDie with no phantom leaves already-placed structure intact")
{
	Local96World w;
	Object *porter = w.spawnAt("L96Porter", 480.0f, 500.0f);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	REQUIRE(dozer != nullptr);

	const ThingTemplate *bldgTmpl = w.w.things.findTemplate("L96Structure");
	REQUIRE(bldgTmpl != nullptr);

	const Coord3D site{ 500.0f, 500.0f, 0.0f };
	Object *structure = dozer->construct(*bldgTmpl, site, 0.0f, *w.playerOf('A'), false);
	REQUIRE(structure != nullptr);
	const ObjectID structureId = structure->getID();

	// Advance until the porter places the phantom (m_unplacedStructure becomes INVALID_ID).
	for (int i = 0; i < 60 && dozer->unplacedStructure() != INVALID_ID; ++i)
	{
		w.frames(1);
	}
	REQUIRE(dozer->unplacedStructure() == INVALID_ID);

	porter->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	w.frames(1);
	// Already-placed structure stays alive on the map.
	Object *stillThere = w.logic->findObjectByID(structureId);
	REQUIRE(stillThere != nullptr);
	CHECK_FALSE(stillThere->isEffectivelyDead());
}

TEST_CASE("LOCAL-96 onDelete (RW 0x0088CF96): deleting a dozer cancels pending tasks")
{
	Local96World w;
	Object *porter = w.spawnAt("L96Porter", 200.0f, 200.0f);
	Object *bldg = w.spawnAt("L96Structure", 500.0f, 500.0f);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	REQUIRE(dozer != nullptr);

	ActiveBody *body = dynamic_cast<ActiveBody *>(bldg->getBodyModule());
	REQUIRE(body != nullptr);
	body->internalChangeHealth(-100.0f);
	REQUIRE(dozer->repair(*bldg));
	REQUIRE(dozer->isTaskPending(DozerAIUpdate::DOZER_TASK_REPAIR));

	dozer->onDelete();
	CHECK_FALSE(dozer->isTaskPending(DozerAIUpdate::DOZER_TASK_REPAIR));
	CHECK(dozer->getCurrentTask() == DozerAIUpdate::DOZER_TASK_INVALID);
}

TEST_CASE("LOCAL-96 canAcceptNewRepair (RW 0x0088CB8A / RW 0x008AE0FE): null structure is refused")
{
	Local96World w;
	Object *porter = w.spawnAt("L96Porter", 200.0f, 200.0f);
	Object *workerObj = w.spawnAt("L96Worker", 240.0f, 200.0f);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	WorkerAIUpdate *worker = dynamic_cast<WorkerAIUpdate *>(workerObj->getAIUpdateInterface());
	REQUIRE(dozer != nullptr);
	REQUIRE(worker != nullptr);

	CHECK_FALSE(dozer->canAcceptNewRepair(nullptr));
	CHECK_FALSE(worker->canAcceptNewRepair(nullptr));
}

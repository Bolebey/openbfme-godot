// OpenBFME. GPL-3.0.
// LOCAL-109 (fork issue 787): builders and walls second pass from RotWK 2.01 game.dat
// (md5 daed3668006cd90f01c34e5a7da1901f):
//   * DozerAIUpdate::loadPostProcess (RW 0x0088D186, DONOR BFME2 DozerAIUpdateCtor.cpp:494):
//     calls AIUpdateInterface::loadPostProcess (RW 0x00667BEF), then if m_unplacedStructure (+0x568) != 0
//     tail-calls makePhantomStructureInert (RW 0x0088D0D8): clears dangling id if missing, else sets
//     effectively dead (RW 0x0068D950(1)) and removes from world if inWorld (RW 0x0068C18F).
//   * ManTheWallsSpecialPower group-move dispatcher (RW 0x008CB5A6, DONOR BFME2 0x004C7167):
//     issues AIGroup::groupMoveToPosition (RW 0x00774897) with CMD_FROM_PLAYER (0 at RW 0x008CB5E1),
//     not CMD_FROM_AI, and balances hordes across markers via RW 0x008CBACB.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "ProdTestUtil.h"

#include "Common/Player.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kLocal109Ini[] =
	"Object L109Porter\n"
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
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"End\n"
	"Object L109Structure\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  BuildCost = 250\n"
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

struct Local109World : combattest::CombatWorld
{
	Local109World() : CombatWorld(kLocal109Ini)
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

TEST_CASE("LOCAL-109 DozerAIUpdate::loadPostProcess (RW 0x0088D186): enforces phantom inertness and clears dangling phantom id")
{
	// Source of expectations: RotWK 2.01 RW 0x0088D186 -> RW 0x0088D0D8 (makePhantomStructureInert).
	Local109World w;
	Object *porter = w.spawnAt("L109Porter", 200.0f, 200.0f);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	REQUIRE(dozer != nullptr);

	const ThingTemplate *bldgTmpl = w.w.things.findTemplate("L109Structure");
	REQUIRE(bldgTmpl != nullptr);

	// Order construction at a distant site so the phantom remains unplaced.
	Object *phantom = dozer->construct(*bldgTmpl, Coord3D{ 500.0f, 200.0f, 0.0f }, 0.0f, *w.playerOf('A'), false);
	REQUIRE(phantom != nullptr);
	REQUIRE(dozer->unplacedStructure() == phantom->getID());
	REQUIRE(phantom->isEffectivelyDead());
	REQUIRE_FALSE(phantom->isInWorld());

	// Simulate post-load state where the phantom was re-entered into the world / marked alive:
	// DozerAIUpdate::loadPostProcess (RW 0x0088D186 -> RW 0x0088D0D8) must restore inertness.
	w.logic->friend_containEnterWorld(*phantom);
	phantom->friend_setEffectivelyDead(false);
	REQUIRE(phantom->isInWorld());
	REQUIRE_FALSE(phantom->isEffectivelyDead());

	dozer->loadPostProcess();
	CHECK(phantom->isEffectivelyDead());
	CHECK_FALSE(phantom->isInWorld());
	CHECK(dozer->unplacedStructure() == phantom->getID());

	// Neighboring behavior: if the phantom object vanished before loadPostProcess (RW 0x0088D0F4..0x0088D0F8),
	// loadPostProcess clears m_unplacedStructure to INVALID_ID without crashing.
	w.logic->destroyObject(phantom);
	w.frames(1);
	dozer->loadPostProcess();
	CHECK(dozer->unplacedStructure() == INVALID_ID);
}

#include "Common/SpecialPower.h"
#include "GameLogic/Module/SpecialPowerModules.h"

namespace
{
const char kLocal109ManTheWallsIni[] =
	"SpecialPower SpecialAbilityL109ManTheWalls\n"
	"  Enum = SPECIAL_MAN_THE_WALLS\n"
	"  ReloadTime = 1000\n"
	"End\n"
	"Object L109Keep\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = ManTheWallsSpecialPower ModuleTag_MTW\n"
	"    SpecialPowerTemplate = SpecialAbilityL109ManTheWalls\n"
	"  End\n"
	"End\n"
	"Object L109WallMarker\n"
	"  KindOf = TACTICAL_MARKER IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 5\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object L109Horde\n"
	"  KindOf = INFANTRY SELECTABLE HORDE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 15\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("LOCAL-109 ManTheWallsSpecialPower (RW 0x008CB5A6): issues CMD_FROM_PLAYER group move and balances hordes across markers (RW 0x008CBACB)")
{
	// Source of expectations: RotWK 2.01 RW 0x008CB5A6 (0x8CB5E1 pushes 0 = CMD_FROM_PLAYER to RW 0x00774897)
	// and RW 0x008CBACB (0x8CBB02..0x8CBB24 balances hordes across sorted nearest markers).
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	TheSpecialPowerStore = &powers;
	struct RestorePowers { SpecialPowerStore *saved; ~RestorePowers() { TheSpecialPowerStore = saved; } } restorePowers{ savedPowers };
	combattest::CombatWorld w("");
	w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
	SpecialPowerModules::registerAll(w.w.modules);
	const std::string err = w.w.load(kLocal109ManTheWallsIni, INI_LOAD_OVERWRITE, "local109_mtw.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	Object *keep = w.spawn("L109Keep", 100.0f, 100.0f, 0.0f, w.teamA());
	Object *markerA = w.spawn("L109WallMarker", 400.0f, 100.0f, 0.0f, w.teamA());
	Object *markerB = w.spawn("L109WallMarker", 400.0f, 300.0f, 0.0f, w.teamA());
	Object *horde1 = w.spawn("L109Horde", 200.0f, 110.0f, 0.0f, w.teamA());
	Object *horde2 = w.spawn("L109Horde", 200.0f, 120.0f, 0.0f, w.teamA());
	REQUIRE(keep != nullptr);
	REQUIRE(markerA != nullptr);
	REQUIRE(markerB != nullptr);
	REQUIRE(horde1 != nullptr);
	REQUIRE(horde2 != nullptr);

	w.frames(2);
	REQUIRE(horde1->getAIUpdateInterface()->lastCommandSource() == CMD_FROM_AI);
	REQUIRE(horde2->getAIUpdateInterface()->lastCommandSource() == CMD_FROM_AI);

	auto *mtw = dynamic_cast<ManTheWallsSpecialPower *>(keep->findModule("ManTheWallsSpecialPower"));
	REQUIRE(mtw != nullptr);
	mtw->doSpecialPower(0);

	// RW 0x008CB5A6 passes CMD_FROM_PLAYER (0 at 0x8CB5E1) to AIGroup::groupMoveToPosition (RW 0x00774897).
	CHECK(horde1->getAIUpdateInterface()->lastCommandSource() == CMD_FROM_PLAYER);
	CHECK(horde2->getAIUpdateInterface()->lastCommandSource() == CMD_FROM_PLAYER);

	// RW 0x008CBACB balances the two hordes across the two markers (one per marker on pass count=0),
	// so after a few frames one moves toward y=100 (markerA) and the other toward y=300 (markerB).
	const float y1Before = horde1->getPosition()->y;
	const float y2Before = horde2->getPosition()->y;
	w.frames(10);
	CHECK(horde1->getPosition()->x > 200.0f); // tolerance: distance depends on locomotor speed and frames stepped
	CHECK(horde2->getPosition()->x > 200.0f); // tolerance: distance depends on locomotor speed and frames stepped
	const bool oneMovedNorth = (horde1->getPosition()->y > y1Before + 5.0f) || (horde2->getPosition()->y > y2Before + 5.0f);
	CHECK(oneMovedNorth);
}

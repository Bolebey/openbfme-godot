// OpenBFME unit tests for Local 142 (fork issue 859): game-logic stops closed by mechanism.
// Synthetic INI only; every expectation comes from RotWK 2.01 game.dat disassembly (MD5 daed3668006cd90f01c34e5a7da1901f).

#include "doctest.h"
#include "MoveTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/StealthAbilityModules.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameClient/ClientEvents.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Module/DockUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"

namespace
{
const char *kS7605Powers =
	"SpecialPower SP_L142_Grab\n"
	"  Enum = SPECIAL_GRAB_PASSENGER\n"
	"  ReloadTime = 1000\n"
	"End\n"
	"SpecialPower SP_L142_Normal\n"
	"  Enum = SPECIAL_GENERAL_TARGETLESS_TWO\n"
	"  ReloadTime = 1000\n"
	"End\n";

const char *kS7605Objects =
	"Object L142_Rider\n"
	"  KindOf = INFANTRY\n"
	"  TransportSlotCount = 1\n"
	"  Body = ActiveBody B\n    MaxHealth = 10\n  End\n"
	"End\n"
	"Object L142_GrabWithContain\n"
	"  KindOf = INFANTRY\n"
	"  Body = ActiveBody B\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface AI\n  End\n"
	"  Behavior = SpecialPowerModule SPM\n"
	"    SpecialPowerTemplate = SP_L142_Grab\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = ToggleHiddenSpecialAbilityUpdate TH\n"
	"    SpecialPowerTemplate = SP_L142_Grab\n"
	"  End\n"
	"  Behavior = TransportContain TC\n"
	"    Slots = 1\n"
	"  End\n"
	"End\n"
	"Object L142_GrabNoContain\n"
	"  KindOf = INFANTRY\n"
	"  Body = ActiveBody B\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface AI\n  End\n"
	"  Behavior = SpecialPowerModule SPM\n"
	"    SpecialPowerTemplate = SP_L142_Grab\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = ToggleHiddenSpecialAbilityUpdate TH\n"
	"    SpecialPowerTemplate = SP_L142_Grab\n"
	"  End\n"
	"End\n"
	"Object L142_NormalNoContain\n"
	"  KindOf = INFANTRY\n"
	"  Body = ActiveBody B\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface AI\n  End\n"
	"  Behavior = SpecialPowerModule SPM\n"
	"    SpecialPowerTemplate = SP_L142_Normal\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = ToggleHiddenSpecialAbilityUpdate TH\n"
	"    SpecialPowerTemplate = SP_L142_Normal\n"
	"  End\n"
	"End\n";

struct Local142PowerStore
{
	SpecialPowerStore powers;
	SpecialPowerStore *saved = TheSpecialPowerStore;
	explicit Local142PowerStore(movetest::MoveWorld &w)
	{
		TheSpecialPowerStore = &powers;
		w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		const std::string err = w.w.load(kS7605Powers, INI_LOAD_OVERWRITE, "local142_powers.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Local142PowerStore() { TheSpecialPowerStore = saved; }
};
} // namespace

// RW 0x851B6E (SpecialPowerUpdateInterface vslot 0x20, called by ToggleHiddenSpecialAbilityUpdate::hide RW 0x8B1A84):
// for SPECIAL_GRAB_PASSENGER (0x27) / SPECIAL_GRAB_CHUNK (0x28), RW 0x851BA2..0x851BC6 checks
// object->getContain() != null && (unsigned)getContainCount() < (unsigned)getContainMax().
TEST_CASE("LOCAL-142 S-7605: ToggleHiddenSpecialAbilityUpdate::hide checks contain room for grab enums (RW 0x851BA2..0x851BC6)")
{
	movetest::MoveWorld w(20, 20);
	Local142PowerStore powerGuard(w);
	const std::string err = w.w.load(kS7605Objects, INI_LOAD_OVERWRITE, "local142_s7605.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	w.buildMap();
	Team *team = w.players.getNeutralPlayer()->getDefaultTeam();
	const unsigned hiddenStatus = static_cast<unsigned>(CombatNames::status("HIDDEN"));

	Object *withContain = w.spawn("L142_GrabWithContain", 50.0f, 50.0f, 0.0f, team);
	Object *noContain = w.spawn("L142_GrabNoContain", 70.0f, 50.0f, 0.0f, team);
	Object *normalObj = w.spawn("L142_NormalNoContain", 90.0f, 50.0f, 0.0f, team);
	Object *rider = w.spawn("L142_Rider", 50.0f, 60.0f, 0.0f, team);
	REQUIRE(withContain);
	REQUIRE(noContain);
	REQUIRE(normalObj);
	REQUIRE(rider);

	auto *thContain = ToggleHiddenSpecialAbilityUpdate::of(*withContain);
	auto *thNoContain = ToggleHiddenSpecialAbilityUpdate::of(*noContain);
	auto *thNormal = ToggleHiddenSpecialAbilityUpdate::of(*normalObj);
	REQUIRE(thContain);
	REQUIRE(thNoContain);
	REQUIRE(thNormal);

	// Empty container (0 < 1): hide succeeds (RW 0x851BC6 jb -> true)
	thContain->hide();
	CHECK(withContain->testStatus(hiddenStatus));
	// Toggle back off via unhide (RW 0x8B19AC)
	thContain->unhide();
	CHECK_FALSE(withContain->testStatus(hiddenStatus));

	// Fill the single slot (1 < 1 is false): hide refused (RW 0x851BC6)
	REQUIRE(withContain->getContain()->addToContain(rider));
	thContain->hide();
	CHECK_FALSE(withContain->testStatus(hiddenStatus));

	// No container on grab enum: hide refused (RW 0x851BAD je -> false)
	thNoContain->hide();
	CHECK_FALSE(noContain->testStatus(hiddenStatus));

	// Non-grab enum without container: hide succeeds (RW 0x851B9E -> true)
	thNormal->hide();
	CHECK(normalObj->testStatus(hiddenStatus));
}

// RW 0x7C191A (WAREHOUSE_SET_VALUE, action 216) -> RW 0x75A243 (findObjectByName) ->
// RW 0x68BDA5 (findModule "SupplyWarehouseDockUpdate") -> RW 0x8AB64E (setCashValue: ceil(value / ValuePerSupplyBox),
// then RW 0x671948 drawable supply status update).
TEST_CASE("LOCAL-142 S-7534: WAREHOUSE_SET_VALUE sets boxes with ceil(cash/ValuePerSupplyBox) and emits SUPPLY_STATUS (RW 0x7C191A / 0x8AB64E)")
{
	const char *kWarehouseIni =
		"Object L142_Warehouse\n"
		"  KindOf = STRUCTURE\n"
		"  Body = ActiveBody B\n    MaxHealth = 100\n  End\n"
		"  Behavior = SupplyWarehouseDockUpdate D\n"
		"    NumberApproachPositions = -1\n"
		"    StartingBoxes = 5\n"
		"  End\n"
		"End\n";
	movetest::MoveWorld w(20, 20, kWarehouseIni);
	w.buildMap();
	w.logic->economy().settings().valuePerSupplyBox = 100;

	ClientEventRecorder events(*w.logic);
	w.logic->setClientHooks(&events);
	struct Unhook { GameLogic *logic; ~Unhook() { logic->setClientHooks(nullptr); } } unhook{ w.logic.get() };

	Object *ware = w.spawn("L142_Warehouse", 50.0f, 50.0f, 0.0f, w.players.getNeutralPlayer()->getDefaultTeam());
	REQUIRE(ware);
	ScriptEngine::SideScripts side;
	side.sideName = "";
	static ScriptList emptyList;
	side.lists.push_back(&emptyList);
	w.logic->scriptEngine().newGame({ side }, nullptr, nullptr, false);

	ware->setName("L142_Ware1");
	auto *dock = dynamic_cast<SupplyWarehouseDockUpdate *>(ware->findModule("SupplyWarehouseDockUpdate"));
	REQUIRE(dock);
	CHECK(dock->boxesStored() == 5);

	auto makeWarehouseAction = [](const char *objName, int cash) {
		ScriptActionRec a;
		a.type = ScriptTemplates::findAction("WAREHOUSE_SET_VALUE");
		a.resolved = a.type;
		a.enabled = true;
		ScriptParameter p0;
		p0.type = 14;
		p0.stringValue = objName;
		ScriptParameter p1;
		p1.type = 0;
		p1.intValue = cash;
		a.params = { p0, p1 };
		return a;
	};

	// 250 / 100 = 2.5 -> ceil = 3 boxes (RW 0x8AB64E)
	ScriptActions::execute(w.logic->scriptEngine(), makeWarehouseAction("L142_Ware1", 250));
	CHECK(dock->boxesStored() == 3);
	CHECK(w.logic->scriptEngine().stats().unportedActions.count("WAREHOUSE_SET_VALUE") == 0);
	bool sawSupply3 = false;
	for (const ClientEvent &e : events.take())
	{
		if (e.kind == ClientEvent::SUPPLY_STATUS && e.supplyCurrentBoxes == 3 && e.supplyStartingBoxes == 5)
		{
			sawSupply3 = true;
		}
	}
	CHECK(sawSupply3);

	// Exact multiple: 200 / 100 = 2 boxes
	ScriptActions::execute(w.logic->scriptEngine(), makeWarehouseAction("L142_Ware1", 200));
	CHECK(dock->boxesStored() == 2);

	// Unknown object name: no crash, warehouse untouched (RW 0x7C1943 je)
	ScriptActions::execute(w.logic->scriptEngine(), makeWarehouseAction("L142_NoSuchObject", 900));
	CHECK(dock->boxesStored() == 2);
}

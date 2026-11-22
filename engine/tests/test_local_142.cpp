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
#include "GameLogic/Object/Object.h"

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

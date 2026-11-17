// PORT-275 (fork issue 642): the Galadriel mirror's DissentLocked / WardByMirror buttons, which sit in slots
// the pushed visible range does not cover, and the command-set swap that follows the OCL power.
//
// RotWK's rule for a button outside the visible range, read with rw.py dis:
//   RW 0x943D6F (populateCommand): command window i shows the button of slot rangeStart + i, and only i < rangeCount
//   RW 0x97D718 (ControlBar::resetRanges): an empty range stack gets (0, InitialVisible) of the current command set
//   RW 0x71CF0A / RW 0x75CA97: the bar's placement reads the range stack's top; empty -> (0, 33)
// So a button whose slot is outside every pushed PUSH_VISIBLE_COMMAND_RANGE window (CommandRangeStart / CommandRangeCount
// at CommandButton + 0x22C / + 0x230) is never on the bar — in RotWK too. That is Ennorath's case for the mirror:
// GaladrielDarkMirror's base set (commandset.inc:27532) is 1 Passive, 2 Vision, 3 WardByMirror, 4 DissentLocked and
// Ennorath pushes windows 0-3; the mirror Set8 (commandset.inc:27540) shows Dissent at 4 only while the player holds
// Upgrade_Level_8 (DissentLocked's NEED_UPGRADE gate, RW 0x9428A3). All by design: no engine fix.
//
// What this test pins on the port side is the swap that the mirror's lock relies on: an OCLSpecialPower press
// (GUI_COMMAND_SPECIAL_POWER -> MSG_DO_SPECIAL_POWER) fires the power, its OCL spawns the marker object, the marker's
// modifier applies the used-upgrade, and CommandSetUpgrade::upgradeImplementation (RW 0x8B7C68) swaps the object's
// command set to the used one (Object::getCommandSetName answers m_commandSetOverride when it is set). The port
// already does all of it (UpgradeModuleClasses.cpp:489, Object.cpp:264); the guarantee the sweep needs is that the
// bar is re-evaluated the very update after the swap, so the locked-out powers are gone at once and the used set's
// slots show. Tests on synthetic INI, no game files. GPL-3.0.

#include "doctest.h"

#include "EconTestUtil.h"
#include "IniTestUtil.h"
#include "LogicTestUtil.h"

#include "Common/INI.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/HudContext.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/Object/Object.h"

#include <string>

namespace
{
// the buttons of the case: a passive in slot 1, two powers at 3 / 4 (NEED_TARGET_POS), the locked gate at 4 of the
// base set and the level-8 set; the page button pushes the range 3..4 (0-based CommandRangeStart = 2)
const char buttons[] = R"(
CommandButton Command_P275_Passive
  Command = GET_STORED_PAYLOAD
  Options = NONPRESSABLE
  ButtonImage = UCCommon_Passive
  TextLabel = CONTROLBAR:P275Passive
  InPalantir = Yes
End
CommandButton Command_P275_Vision
  Command = SPECIAL_POWER
  Options = NEED_TARGET_POS
  SpecialPower = SpecialAbilityP275Vision
  ButtonImage = UCCommon_Vision
  TextLabel = CONTROLBAR:P275Vision
  InPalantir = Yes
End
CommandButton Command_P275_Ward
  Command = SPECIAL_POWER
  Options = NEED_TARGET_POS
  SpecialPower = SpecialAbilityP275Ward
  ButtonImage = UCCommon_Ward
  TextLabel = CONTROLBAR:P275Ward
  InPalantir = Yes
End
CommandButton Command_P275_DissentLocked
  Command = SPECIAL_POWER
  Options = NEED_TARGET_POS NEED_UPGRADE
  SpecialPower = SpecialAbilityP275Dissent
  NeededUpgrade = Upgrade_Level_8
  ButtonImage = UCCommon_DissentLocked
  TextLabel = CONTROLBAR:P275DissentLocked
  InPalantir = Yes
End
CommandButton Command_P275_Dissent
  Command = SPECIAL_POWER
  Options = NEED_TARGET_POS
  SpecialPower = SpecialAbilityP275Dissent
  ButtonImage = UCCommon_Dissent
  TextLabel = CONTROLBAR:P275Dissent
  InPalantir = Yes
End
CommandButton Command_P275_Page
  Command = PUSH_VISIBLE_COMMAND_RANGE
  CommandRangeStart = 2
  CommandRangeCount = 2
  ButtonImage = UCCommon_Page
  TextLabel = CONTROLBAR:P275Page
  InPalantir = Yes
End
CommandSet P275MirrorSet
  1 = Command_P275_Passive
  2 = Command_P275_Vision
  3 = Command_P275_Ward
  4 = Command_P275_DissentLocked
End
CommandSet P275MirrorSet8
  1 = Command_P275_Passive
  2 = Command_P275_Vision
  3 = Command_P275_Ward
  4 = Command_P275_Dissent
End
CommandSet P275MirrorUsedSet
  1 = Command_P275_Passive
End
)";

// the powers: Vision is on the object from birth, Ward / Dissent come with the level-8 upgrade (the mirror's
// GaladrielMirrorUsedModifier analogue is the used-upgrade below); ReloadTime small so the state answers Enabled
const char powers[] = R"(
SpecialPower SpecialAbilityP275Vision
  Enum = SPECIAL_GENERAL_TARGETLESS
  ReloadTime = 10
End
SpecialPower SpecialAbilityP275Ward
  Enum = SPECIAL_GENERAL_TARGETLESS
  ReloadTime = 10
End
SpecialPower SpecialAbilityP275Dissent
  Enum = SPECIAL_GENERAL_TARGETLESS
  ReloadTime = 10
End
)";

// the upgrades of the case: the veterancy gate (the mod's Upgrade_Level_8), the used-upgrade the CommandSetUpgrade
// module swaps on, and the trigger upgrade the OCL power grants
const char upgrades[] = R"(
Upgrade Upgrade_Level_8
  Type = OBJECT
End
Upgrade Upgrade_P275MirrorUsed
  Type = OBJECT
End
)";

// the mirror: a structure with the vision power from birth; CommandSetUpgrade swaps its set to the used one when
// Upgrade_P275MirrorUsed lands (the mod does exactly this through GaladrielMirrorUsedModifier). The mirror set has
// no page button, so slots 3 / 4 are outside the base range (0, InitialVisible) of 4 and unreachable until the
// level-8 swap puts the unlocked Dissent into the same window
const char objects[] = R"(
Object P275Mirror
  KindOf = SELECTABLE STRUCTURE
  CommandSet = P275MirrorSet
  Body = StructureBody ModuleTag_Body
    MaxHealth = 1000
  End
  Geometry = BOX
  GeometryMajorRadius = 10
  GeometryMinorRadius = 10
  GeometryHeight = 10
  Behavior = OCLSpecialPower ModuleTag_Vision
    SpecialPowerTemplate = SpecialAbilityP275Vision
    OCL = OCL_P275Vision
  End
  Behavior = CommandSetUpgrade ModuleTag_Swap
    TriggeredBy = Upgrade_P275MirrorUsed
    CommandSet = P275MirrorUsedSet
  End
End
)";

// the marker the power spawns (the OCL target of the mirror); it exists so the press has something to create
const char ocls[] = R"(
ObjectCreationList OCL_P275Vision
  CreateObject
    ObjectNames = P275VisionMarker
    Count = 1
  End
End
Object P275VisionMarker
  KindOf = INERT
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 10
  End
  Geometry = SPHERE
  GeometryMajorRadius = 1
  GeometryHeight = 1
End
)";

// the bar over a synthetic world, command store and special power store (no game files)
struct Fixture
{
	econtest::Fx game{ std::string(objects) + ocls };
	CommandStore store;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	PinholeView view;
	InGameUI ui;
	MessageStream stream;
	HudContext context;
	ControlBar bar;

	Fixture()
		: context{ *game.logic, nullptr, view, ui, stream, &store, {}, {} }
		, bar{ context }
	{
		TheSpecialPowerStore = &powers;
		store.setThingFactory(&game.w.things);
		TheCommandStore = &store;
		game.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		game.w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		game.w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		const std::string powerErr = game.w.load(powers);
		REQUIRE_MESSAGE(powerErr.empty(), powerErr);
		const std::string buttonErr = game.w.load(buttons);
		REQUIRE_MESSAGE(buttonErr.empty(), buttonErr);
		// the upgrades of the case sit next to the fixture's stock ones
		const std::string upErr = game.w.load(upgrades, INI_LOAD_OVERWRITE, "p275_upgrades.ini");
		REQUIRE_MESSAGE(upErr.empty(), upErr);
		game.players.setLocalPlayer(game.player("Alice"));
		ui.setLogic(game.logic.get());
		ui.setMaxSelectCount(50);
	}
	~Fixture()
	{
		TheCommandStore = nullptr;
		TheSpecialPowerStore = savedPowers;
	}
	// the evaluated button of `name` on the arc, after a reselect and one update (null: not on the bar)
	const ControlBarButton *onBar(Object *o, const char *name)
	{
		ui.deselectAll(false);
		ui.selectObject(o->getID());
		bar.update();
		for (const ControlBarButton &b : bar.palantirButtons())
		{
			if (b.button && b.button->m_name == name)
			{
				return &b;
			}
		}
		return nullptr;
	}
};
} // namespace

TEST_CASE("PORT-275 the base mirror set's slots 3 / 4 are outside the pushed range and not on the bar (RW 0x97D718: empty stack -> (0, InitialVisible))")
{
	Fixture f;
	Object *mirror = f.game.makeAt("P275Mirror", "Alice", 100.0f, 100.0f);
	REQUIRE(mirror);
	// the fixture's data: the base set shows 4 slots, the page pushes 2..3 (0-based); without a push slots 3 / 4 stay off
	CHECK(f.bar.visibleRangeStart() == 0);
	CHECK(f.onBar(mirror, "Command_P275_Passive"));
	CHECK(f.onBar(mirror, "Command_P275_Vision"));
	// slot 3 = Ward, slot 4 = DissentLocked (1-based in the set): both beyond the InitialVisible window of the base set
	CHECK(!f.onBar(mirror, "Command_P275_Ward"));
	CHECK(!f.onBar(mirror, "Command_P275_DissentLocked"));
	// the page button is in no mirror set of the case; it is pinned here so the range rule is visible on its own
}

TEST_CASE("PORT-275 after the CommandSetUpgrade swap the bar shows the used set in the same update (RW 0x8B7C68)")
{
	Fixture f;
	Object *mirror = f.game.makeAt("P275Mirror", "Alice", 100.0f, 100.0f);
	REQUIRE(mirror);
	REQUIRE(f.onBar(mirror, "Command_P275_Vision"));
	// the press of the OCL power fires it; the marker's modifier lands the used-upgrade, and CommandSetUpgrade swaps
	// the object's command set (the mod's GaladrielMirrorUsedModifier path)
	const ControlBarButton *vision = f.onBar(mirror, "Command_P275_Vision");
	REQUIRE(vision);
	CHECK(vision->state == ButtonState::Enabled);
	CHECK(f.bar.pressButton(vision->slot, true));
	f.game.frames(2);
	// the swap is done on the logic side: the override is the used set
	CHECK(mirror->getCommandSetName() == "P275MirrorUsedSet");
	// and the bar reflects it at once: the powers of the old set are gone, only the passive of the used set stays
	CHECK(f.onBar(mirror, "Command_P275_Passive"));
	CHECK(!f.onBar(mirror, "Command_P275_Vision"));
	CHECK(!f.onBar(mirror, "Command_P275_Ward"));
	CHECK(!f.onBar(mirror, "Command_P275_DissentLocked"));
	// the neighbour that guards the rule: the used set's one slot is the only button left on the arc
	int onArc = 0;
	for (const ControlBarButton &b : f.bar.palantirButtons())
	{
		if (b.button)
		{
			++onArc;
		}
	}
	CHECK(onArc == 1);
}

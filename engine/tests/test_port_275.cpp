// PORT-275 (fork issue 642): the Galadriel mirror's WardByMirror / Dissent(Locked) buttons, which the button
// matrix finds NOT-ON-BAR, and the command-set swap that the mirror's lock relies on.
//
// RotWK's rules for a button that is not on the bar, read with rw.py dis:
//   RW 0x97D718 (ControlBar::resetRanges): an empty range stack gets (0, InitialVisible) of the current command set
//   RW 0x71CF0A / RW 0x75CA97: the bar's placement reads the range stack's top; empty -> (0, 33)
//   RW 0x943D6F (populateCommand): command window i shows the button of slot rangeStart + i, only i < rangeCount
//   RW 0x92FF5C / RW 0x92F082: a button reaches the Palantir arc only InPalantir (+0x102) in windows 0..5, the side
//                              bar only Radial (+0x101); a button with neither flag is off the bar
// So a button whose slot is outside every pushed PUSH_VISIBLE_COMMAND_RANGE window (CommandRangeStart /
// CommandRangeCount at CommandButton + 0x22C / + 0x230), or past InitialVisible of the set the object wears, is
// never on the bar — in RotWK exactly as in the port. That is Ennorath's case for the mirror (its sets and the
// buttons' flags are the mod's data, docs/ennorath/PORT275_RW_ANALYSIS.md), so there is no engine fix.
//
// What these tests pin on the port side is the swap that the mirror's lock relies on: CommandSetUpgrade::
// upgradeImplementation (RW 0x8B7C68) sets the object's command set override (Object::getCommandSetName answers
// m_commandSetOverride when it is set, Object.cpp), and the bar shows the swapped set's buttons at the very next
// update — the one the frame after the upgrade lands already uses. Tests on synthetic INI, no game files. GPL-3.0.

#include "doctest.h"

#include "EconGameData.h"
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
// the buttons of the case: a passive in slot 1, a power at slot 2; the base set's slots 3 / 4 sit past its
// InitialVisible = 2 (the mod's mirror sets hide the same way); the used set holds the passive alone; the swapped
// "level-8" set shows the formerly locked power at slot 2
const char buttons[] = R"(
CommandButton Command_P275_Passive
  Command = STOP
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
CommandSet P275MirrorSet
  1 = Command_P275_Passive
  2 = Command_P275_Vision
  3 = Command_P275_Ward
  4 = Command_P275_DissentLocked
  InitialVisible = 2
End
CommandSet P275MirrorSet8
  1 = Command_P275_Passive
  2 = Command_P275_Dissent
End
CommandSet P275MirrorUsedSet
  1 = Command_P275_Passive
End
)";

// the powers: Vision is the base set's, Ward / Dissent the hidden and the swapped-in ones; a short ReloadTime so the
// evaluation answers Enabled
const char powerIni[] = R"(
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
// module swaps on, and the level-8 upgrade whose TriggeredBy brings the unlocked set
const char upgrades[] = R"(
Upgrade Upgrade_Level_8
  Type = OBJECT
End
Upgrade Upgrade_P275MirrorUsed
  Type = OBJECT
End
Upgrade Upgrade_P275Mirror8
  Type = OBJECT
End
)";

// the mirror: a structure with the vision power from birth; two CommandSetUpgrade modules stand for the mod's two
// swaps — the level-8 one (the locked power's set) and the used one (the lock after a cast, the mod does it through
// GaladrielMirrorUsedModifier, attributemodifier.inc:15996)
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
  Behavior = SpecialPowerModule ModuleTag_Vision
    SpecialPowerTemplate = SpecialAbilityP275Vision
  End
  Behavior = CommandSetUpgrade ModuleTag_Swap8
    TriggeredBy = Upgrade_P275Mirror8
    CommandSet = P275MirrorSet8
  End
  Behavior = CommandSetUpgrade ModuleTag_SwapUsed
    TriggeredBy = Upgrade_P275MirrorUsed
    CommandSet = P275MirrorUsedSet
  End
End
)";

// the bar over a synthetic world, command store and special power store (no game files)
struct Fixture
{
	// the upgrades of the case load with the fixture's stock ones, before the objects name them in TriggeredBy
	struct World : logictest::LogicWorld
	{
		World()
			: logictest::LogicWorld(RandomAlgorithm::ZH_CarryChain, upgrades)
		{
			REQUIRE_MESSAGE(loadError.empty(), loadError);
			LogicModules::registerAll(w.modules);
			const std::string err = w.load(objects);
			REQUIRE_MESSAGE(err.empty(), err);
			logic->settings().bodyThresholdsLoaded = true;
			logic->settings().unitDamagedThreshold = 0.65f;
			logic->settings().unitReallyDamagedThreshold = 0.4f;
			std::string e;
			REQUIRE_MESSAGE(EconomySettings::scan(econtest::kGameData, logic->economy().settings(), &e), e);
			logic->economy().initAllCommandPoints();
		}
		Object *makeAt(const char *templateName, const char *owner, float x, float y)
		{
			Object *o = make(templateName, teamOf(owner), INVALID_ID, false);
			Coord3D p{ x, y, 0.0f };
			o->setPosition(&p);
			completeHorde(o);
			return o;
		}
		void frames(int n)
		{
			for (int i = 0; i < n; ++i)
			{
				logic->runLogicFrame();
			}
		}
	};
	World game;
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
		const std::string powerErr = game.w.load(powerIni);
		REQUIRE_MESSAGE(powerErr.empty(), powerErr);
		const std::string buttonErr = game.w.load(buttons);
		REQUIRE_MESSAGE(buttonErr.empty(), buttonErr);
		game.players.setLocalPlayer(game.players.findPlayerWithName("Alice"));
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
	int onArcCount()
	{
		int n = 0;
		for (const ControlBarButton &b : bar.palantirButtons())
		{
			if (b.button)
			{
				++n;
			}
		}
		return n;
	}
};
} // namespace

TEST_CASE("PORT-275 a set's slots past its InitialVisible are not on the bar, as in RotWK (RW 0x97D718 -> (0, InitialVisible), RW 0x943D6F shows windows < rangeCount)")
{
	Fixture f;
	Object *mirror = f.game.makeAt("P275Mirror", "Alice", 100.0f, 100.0f);
	REQUIRE(mirror);
	// the fixture's data: the base set declares InitialVisible = 2, so with the empty range stack only the first
	// two slots are iterated (the port keeps the stack at (0, 33) and clamps by InitialVisible as RW 0x97D718's
	// (0, InitialVisible) window does)
	CHECK(f.onBar(mirror, "Command_P275_Passive"));
	CHECK(f.onBar(mirror, "Command_P275_Vision"));
	// slots 3 / 4 (1-based) are outside the window and there is no push button: not on the bar, exactly as RotWK
	// leaves the mirror's WardByMirror / DissentLocked (the sweep's NOT-ON-BAR rows)
	CHECK(!f.onBar(mirror, "Command_P275_Ward"));
	CHECK(!f.onBar(mirror, "Command_P275_DissentLocked"));
	// the neighbour that guards the rule: only the two in-range buttons are on the arc
	CHECK(f.onArcCount() == 2);
}

TEST_CASE("PORT-275 the CommandSetUpgrade swap's set is on the bar at the very next update (RW 0x8B7C68)")
{
	Fixture f;
	Object *mirror = f.game.makeAt("P275Mirror", "Alice", 100.0f, 100.0f);
	REQUIRE(mirror);
	// before the swap: the base set, the locked power out of reach
	REQUIRE(f.onBar(mirror, "Command_P275_Vision"));
	CHECK(!f.onBar(mirror, "Command_P275_Dissent"));
	// the level-8 upgrade lands (the mod's veterancy); CommandSetUpgrade swaps the set (RW 0x8B7C68 ->
	// Object::setCommandSetOverride RW 0x693B94)
	mirror->giveUpgrade("Upgrade_P275Mirror8");
	CHECK(mirror->getCommandSetName() == "P275MirrorSet8");
	// one update later the bar shows the swapped set: the unlocked Dissent is on the bar, the base set's power is gone
	const ControlBarButton *dissent = f.onBar(mirror, "Command_P275_Dissent");
	REQUIRE(dissent);
	CHECK(dissent->state == ButtonState::Enabled);
	CHECK(!f.onBar(mirror, "Command_P275_Vision"));
	CHECK(!f.onBar(mirror, "Command_P275_Ward"));
	// the mirror's lock: the used upgrade swaps to the one-button set, and the bar follows in the same update —
	// the spent mirror offers nothing but the passive (the mod's GaladrielMirrorUsedModifier path)
	mirror->giveUpgrade("Upgrade_P275MirrorUsed");
	CHECK(mirror->getCommandSetName() == "P275MirrorUsedSet");
	CHECK(f.onBar(mirror, "Command_P275_Passive"));
	CHECK(!f.onBar(mirror, "Command_P275_Dissent"));
	CHECK(f.onArcCount() == 1);
}

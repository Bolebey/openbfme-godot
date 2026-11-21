// OpenBFME. GPL-3.0.
// Local 125 (fork issue 809): global settings readers — ControlBarScheme, Mouse tooltip, Upgrade StrategicIcon, ScoredKillEvaAnnouncer.
// Expectations from RotWK 2.01 game.dat (RW addresses cited per case).

#include "doctest.h"
#include "ProdTestUtil.h"

#include "Common/INI.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/HudContext.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/TacticalView.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
void loadSchemesFromText(ControlBarSchemeStore &store, const std::string &text)
{
	INIEnvironment env;
	store.registerBlock(env.blocks);
	INI ini(env);
	ini.loadMemory("controlbarscheme.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
}
} // namespace

// RW 0x7206D5 (ControlBarScheme::init -> 0x720704 BuildUpClockColor -> RW 0x8DBE7E; 0x720796..0x7207C6 ScreenCreationRes multiplier)
// RW 0x7216B6 (ControlBarSchemeManager::setControlBarSchemeByPlayerTemplate -> 0x72174E..0x72175D picks largest ScreenCreationRes.x)
TEST_CASE("LOCAL-125 ControlBarScheme: BuildUpClockColor and ScreenCreationRes (RW 0x7206D5 / 0x7216B6)")
{
	const std::string ini =
		"ControlBarScheme MenLow\n"
		"  ScreenCreationRes = X:800 Y:600\n"
		"  Side = Men\n"
		"  BuildUpClockColor = R:10 G:20 B:30 A:128\n"
		"End\n"
		"ControlBarScheme MenHigh\n"
		"  ScreenCreationRes = X:1024 Y:768\n"
		"  Side = Men\n"
		"  BuildUpClockColor = R:40 G:80 B:120 A:200\n"
		"End\n";

	ControlBarSchemeStore store;
	loadSchemesFromText(store, ini);

	// RW 0x72174E..0x72175D: among schemes for the same side, largest ScreenCreationRes.x wins
	const ControlBarScheme *chosen = store.findSchemeBySide("Men");
	REQUIRE(chosen != nullptr);
	CHECK(chosen->m_name == "MenHigh");
	CHECK(chosen->m_screenCreationRes.x == 1024);
	CHECK(chosen->m_screenCreationRes.y == 768);

	prodtest::ProdWorld f;
	PinholeView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{ *f.logic, nullptr, view, ui, stream, &f.commands, MouseSettings{} };
	ctx.schemes = &store;
	ControlBar bar(ctx);
	bar.setDisplaySize(2048, 1536);
	REQUIRE(bar.setSchemeBySide("Men"));

	// RW 0x720704 -> RW 0x8DBE7E: BuildUpClockColor applied to ControlBar
	CHECK(bar.buildUpClockColor() == chosen->m_buildUpClockColor);
	// RW 0x7207B1 / 0x7207C1: display / ScreenCreationRes multiplier
	CHECK(bar.schemeMultiplierX() == 2.0f);
	CHECK(bar.schemeMultiplierY() == 2.0f);
}

#include "GameClient/GUI/CursorTooltip.h"

// RW 0x5EE1A6 (Mouse::setCursorTooltip): TooltipBackgroundColor (+0x12BC), UseTooltipAltTextColor (+0x12E0),
// UseTooltipAltBackColor (+0x12E1), AdjustTooltipAltColor (+0x12E2).
TEST_CASE("LOCAL-125 Mouse tooltip: TooltipBackgroundColor and alt-color flags (RW 0x5EE1A6)")
{
	const std::string mouseIni =
		"Mouse\n"
		"  TooltipFontName = Arial\n"
		"  TooltipFontSize = 10\n"
		"  TooltipFontIsBold = No\n"
		"  TooltipDelayTime = 100\n"
		"  TooltipTextColor = R:220 G:220 B:220 A:255\n"
		"  TooltipBackgroundColor = R:20 G:30 B:40 A:127\n"
		"  UseTooltipAltTextColor = Yes\n"
		"  UseTooltipAltBackColor = Yes\n"
		"  AdjustTooltipAltColor = Yes\n"
		"End\n";

	CursorTooltipSettings s;
	std::string err;
	REQUIRE(CursorTooltipSettings::scan(mouseIni, "", s, &err));
	CHECK(s.backgroundColor == 0x7F141E28u);
	CHECK(s.useAltTextColor == true);
	CHECK(s.useAltBackColor == true);
	CHECK(s.adjustAltColor == true);

	// RW 0x5EE1F7..0x5EE29A with AdjustTooltipAltColor=Yes and alt=(0.0, 0.5, 1.0):
	// text: ((c + 1.0) * 127.5) -> (127, 191, 255), alpha 0xFF
	// back: (c * 127.5)         -> (0, 63, 127),    alpha 0x7F
	CursorTooltip tip;
	tip.setTooltipWithAltColor(u"Hello", 0, 0.0f, 0.5f, 1.0f, s);
	CHECK(tip.activeTextColor() == 0xFF7FBFFFu);
	CHECK(tip.activeBackgroundColor() == 0x7F003F7Fu);

	// RW 0x5EE230 / 0x5EE28B with AdjustTooltipAltColor=No: direct * 255.0
	s.adjustAltColor = false;
	tip.setTooltipWithAltColor(u"Hello", 0, 0.0f, 0.5f, 1.0f, s);
	CHECK(tip.activeTextColor() == 0xFF007FFFu);
	CHECK(tip.activeBackgroundColor() == 0x7F007FFFu);
}

#include "Common/Upgrade.h"

// RW 0x66F58E / 0x66F5B6 (UpgradeTemplate::cacheButtonImage -> +0x90) and WoTR readers RW 0x9FFC42 / 0xA08500
TEST_CASE("LOCAL-125 Upgrade StrategicIcon: resolves War of the Ring icons (RW 0x66F58E / 0x9FFC42)")
{
	const std::string iniText =
		"Upgrade Upgrade_TestArmor\n"
		"  Type = OBJECT\n"
		"  StrategicIcon = UIHeavyArmor\n"
		"End\n"
		"Upgrade Upgrade_TestNoIcon\n"
		"  Type = OBJECT\n"
		"End\n"
		"Upgrade Upgrade_TestBlades\n"
		"  Type = OBJECT\n"
		"  StrategicIcon = UIForgedBlades\n"
		"End\n";

	UpgradeCenter center;
	UpgradeCenter *prev = TheUpgradeCenter;
	TheUpgradeCenter = &center;
	INIEnvironment env;
	center.registerBlock(env.blocks);
	INI ini(env);
	ini.loadMemory("upgrade.ini", std::vector<std::uint8_t>(iniText.begin(), iniText.end()), INI_LOAD_OVERWRITE);
	TheUpgradeCenter = prev;

	const std::vector<std::string> icons = center.strategicIconsForUpgrades(
		{ "Upgrade_TestArmor", "Upgrade_TestNoIcon", "Upgrade_Unknown", "Upgrade_TestBlades" });
	REQUIRE(icons.size() == 2);
	CHECK(icons[0] == "UIHeavyArmor");
	CHECK(icons[1] == "UIForgedBlades");
}

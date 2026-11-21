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

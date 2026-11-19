// OpenBFME unit tests. GPL-3.0.
// PORT-292: the campaign record's horde overload name, from RotWK 2.01.
#include "doctest.h"
#include "LogicTestUtil.h"
#include "GameLogic/CampaignCarryover.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include <string>

namespace
{
std::string hordeObject(const std::string &name, const std::string &kind, const std::string &module, const std::string &body)
{
	return "Object " + name + "\n"
		" KindOf = " + kind + "\n"
		" Body = ImmortalBody BodyTag\n  MaxHealth = 1\n End\n"
		" Behavior = " + module + " ContainTag\n" + body + " End\n"
		"End\n";
}

std::string winningRecord(const std::string &ini, const std::string &objectName)
{
	logictest::LogicWorld world;
	LogicModules::registerAll(world.w.modules);
	const std::string error = world.w.load(ini, INI_LOAD_OVERWRITE, "synthetic.ini");
	REQUIRE_MESSAGE(error.empty(), error);
	Object *object = world.make(objectName, world.teamOf("Alice"), INVALID_ID, false);
	REQUIRE(object);
	object->setCampaignCarryover(true);
	CampaignCarryover::State state;
	CampaignCarryover::onCurrentMissionWon(*world.logic, world.players, state);
	REQUIRE(state.records.size() == 1);
	CHECK(state.records[0].quantity == 1);
	return state.records[0].templateName;
}
}

TEST_CASE("PORT-292: non-empty overload template is recorded for infantry and cavalry hordes")
{
	// RW 0x6917BB -> slot +0x274 RW 0x86D6FD: nonempty +0x280 replaces the horde's own name.
	CHECK(winningRecord(hordeObject("P292Infantry", "HORDE", "HordeContain", "  LivingWorldOverloadTemplate = P292Replacement\n"), "P292Infantry") == "P292Replacement");
	CHECK(winningRecord(hordeObject("P292Cavalry", "HORDE", "HorseHordeContain", "  LivingWorldOverloadTemplate = P292HorseReplacement\n"), "P292Cavalry") == "P292HorseReplacement");
}

TEST_CASE("PORT-292: empty or missing horde interface falls back and HERO takes precedence")
{
	// RW 0x6918C7..0x6918DE: omitted / empty answer and missing interface use the object's template.
	CHECK(winningRecord(hordeObject("P292Plain", "HORDE", "HordeContain", ""), "P292Plain") == "P292Plain");
	CHECK(winningRecord("Object P292NoContain\n KindOf = HORDE\n Body = ImmortalBody BodyTag\n MaxHealth = 1\n End\nEnd\n", "P292NoContain") == "P292NoContain");
	// RW 0x6917DF branches to HERO before HORDE, even when both kind bits are present.
	CHECK(winningRecord(hordeObject("P292Hero", "HERO HORDE", "HordeContain", "  LivingWorldOverloadTemplate = P292Ignored\n"), "P292Hero") == "P292Hero");
}

// OpenBFME unit tests, lane PORT-247 (fork issue 571): TeleportSpecialAbilityUpdate's MaxDistance, the one field of the three the issue lists that
// RotWK 2.01 reads (FlingPassengerSpecialAbilityUpdate/FlingPassengerVelocity and AutoAbilityBehavior/WorkingRadius are parsed and never read;
// docs/ennorath/PORT-247-RW-ANALYSIS.md).
//
// Every expectation is read from vanilla RotWK 2.01 `game.dat` with tools/mod/rw.py dis, never from this engine:
//   * the field table RW 0xC054B0 (rows at 0xC054B0 .. 0xC054E0, sentinel 0xC054F0) puts `MaxDistance` (parser RW 0x42ED00, a plain real) at
//     data + 0xDC; the class's special power update table RW 0xC6490C overrides slot 8 (offset 0x20) with RW 0x896608, where the base table
//     RW 0xC64658 has RW 0x851B6E.
//   * RW 0x896608 (read linearly): a null place (0x89660C), a null data pointer (0x896611), a null object (0x896629) and MaxDistance at or below
//     zero (0x896618 against the zero RW 0xC1B594) fall back to the base; otherwise |place - position|^2 (0x896630 .. 0x896675 over the object's
//     + 0x38 / + 0x3C / + 0x40) above MaxDistance^2 (0x896679, jbe) answers no (0x89667E); within it the base answers (0x896682).
//   * the place reaches the slot from the location requirements RW 0x897E01: 0x897E14 takes the requirements' first argument, 0x897E18 allows it
//     to be null, 0x897E7F pushes it into `call [eax + 0x20]` at 0x897E82. The object form RW 0x82DAAB passes none, so the null branch is real.
//   * the number in the test INI is the mod's own shape: Shelob's tunnel and Karsh's blink carry MaxDistance 1000 / 500 (field audit v13). The
//     test writes its own smaller value so the map's edge (ActionManager RW 0x82D56A) is not what answers.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"

#include <string>

namespace
{
// CombatTestUtil's CombatWorld plus the special power stores and the two module sets these abilities need.
struct Port247World : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	explicit Port247World(const char *objects) : combattest::CombatWorld(std::string(objects).c_str())
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		HeroAbilityModules::registerAll(w.modules);
		const std::string err = w.load(objects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Port247World() { TheSpecialPowerStore = savedPowers; }
};

// A hero shaped like the mod's teleporting heroes (Shelob, Karsh): a body, an AI, the power and the ability that drives it.
#define PORT247_HERO(name, abilityLines, powerLines)                                                                                             \
	"Object " name "\n"                                                                                                                         \
	" KindOf = INFANTRY SELECTABLE HERO\n"                                                                                                      \
	" Geometry = CYLINDER\n GeometryMajorRadius = 8\n GeometryMinorRadius = 8\n GeometryHeight = 20\n"                                          \
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"                                                                                     \
	" Behavior = AIUpdateInterface AiTag\n End\n"                                                                                               \
	" Behavior = SpecialPowerModule PowerTag\n  SpecialPowerTemplate = SpecialAbilityTestTunnel\n End\n"                                        \
	" Behavior = TeleportSpecialAbilityUpdate TunnelTag\n"                                                                                      \
	"  SpecialPowerTemplate = SpecialAbilityTestTunnel\n"                                                                                       \
	"  UnpackTime = 400\n  PackTime = 400\n  ApproachRequiresLOS = No\n  BusyForDuration = 1800\n" abilityLines " End\n" powerLines "End\n"

const char kPort247[] =
	"SpecialPower SpecialAbilityTestTunnel\n  Enum = SPECIAL_BALROG_WINGS\n  ReloadTime = 1000\nEnd\n"
	PORT247_HERO("TunnelHero", "  MaxDistance = 200\n", "")
	PORT247_HERO("NoLimitHero", "", "") // no MaxDistance: the data keeps 0 (the constructor RW 0x89870E zeroes + 0xD0 .. + 0xD8, the parse leaves + 0xDC)
	PORT247_HERO("MountedHero", "  MaxDistance = 200\n", "  RequiredConditions = MOUNTED\n");

struct Rig
{
	Port247World f;
	Object *hero = nullptr;
	TeleportSpecialAbilityUpdate *tp = nullptr;
	explicit Rig(const char *name) : f(kPort247)
	{
		hero = f.unit(name, 'A', 300.0f, 300.0f);
		REQUIRE(hero);
		REQUIRE(hero->getPosition()->x == 300.0f);
		tp = dynamic_cast<TeleportSpecialAbilityUpdate *>(hero->findModule("TeleportSpecialAbilityUpdate"));
		REQUIRE(tp);
	}
	const SpecialPowerTemplate *power() const
	{
		const SpecialPowerTemplate *t = tp->abilityData()->m_specialPowerTemplate;
		REQUIRE(t);
		return t;
	}
};
} // namespace

// RW 0x896608: the ability is refused where the place lies further than MaxDistance from the object; a place inside it passes, and so does the
// base's answer for a null place (0x89660C). Without the override the far place would pass as it does for every other SpecialAbilityUpdate.
TEST_CASE("PORT-247 teleport: a place beyond MaxDistance is refused, one inside it passes (RW 0x896608)")
{
	Rig r("TunnelHero");
	const Coord3D near = { 400.0f, 300.0f, 0.0f }; // 100 from the object, MaxDistance 200
	const Coord3D far = { 700.0f, 300.0f, 0.0f };  // 400 from the object
	CHECK(ActionManager::canDoSpecialPowerAtLocation(*r.hero, near, r.power(), 0u));
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtLocation(*r.hero, far, r.power(), 0u));
	// the interface method the location requirements call (RW 0x897E82): the far place answers no (0x89667E), the null place the base's yes
	CHECK_FALSE(r.tp->allowsSpecialPowerUse(&far));
	CHECK(r.tp->allowsSpecialPowerUse(&near));
	CHECK(r.tp->allowsSpecialPowerUse(nullptr));
}

// The neighbours the override must not change: with no MaxDistance the data's zero takes the 0x896620 branch back to the base, so the same far
// place passes; and the base's RequiredConditions (RW 0x851551) still refuse a place inside MaxDistance when the object does not meet them.
TEST_CASE("PORT-247 teleport: without MaxDistance the far place passes and the base's conditions still rule (RW 0x896620 / 0x851B6E)")
{
	Port247World f(kPort247);
	Object *free = f.unit("NoLimitHero", 'A', 300.0f, 300.0f);
	Object *mounted = f.unit("MountedHero", 'A', 300.0f, 400.0f);
	REQUIRE(free);
	REQUIRE(mounted);
	auto *freeTp = dynamic_cast<TeleportSpecialAbilityUpdate *>(free->findModule("TeleportSpecialAbilityUpdate"));
	auto *mountedTp = dynamic_cast<TeleportSpecialAbilityUpdate *>(mounted->findModule("TeleportSpecialAbilityUpdate"));
	REQUIRE(freeTp);
	REQUIRE(mountedTp);
	const Coord3D far = { 700.0f, 300.0f, 0.0f };
	CHECK(freeTp->abilityData()->m_maxDistance == 0.0f);
	CHECK(ActionManager::canDoSpecialPowerAtLocation(*free, far, freeTp->abilityData()->m_specialPowerTemplate, 0u));
	// MOUNTED is a model condition the hero does not have: the base's RW 0x851551 refuses even the place inside MaxDistance
	const Coord3D nearMounted = { 400.0f, 400.0f, 0.0f };
	CHECK(mountedTp->abilityData()->m_maxDistance == 200.0f);
	CHECK_FALSE(mountedTp->allowsSpecialPowerUse(&nearMounted));
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtLocation(*mounted, nearMounted, mountedTp->abilityData()->m_specialPowerTemplate, 0u));
}

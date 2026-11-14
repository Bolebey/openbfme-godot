// OpenBFME unit tests. PORT-438 (fork issue 438, P-2): the three Gondor spell book summons of Ennorath -- Lehen (SpellBookLehenAllies),
// DieGraueSchaar (SpellBookDieGraueSchaar) and SummonRohanGondor (SpellBookSummonRohanGondor) -- cast on an empty point and next to objects.
//
// WHAT THE MOD'S POWERS ARE (docs/ennorath/FactionMen.md, "why the three powers were never checked", and FactionMen_Units.md, which carry the mod's own
// SpecialPower and ObjectCreationList text): each of the three names one OCLSpecialPower module whose template forbids objects around the aim point --
// Flags = NO_FORBIDDEN_OBJECTS, ForbiddenObjectFilter = NO_SUMMON_NEAR_OBJECT_FILTER_SPELLBOOK (infantry, cavalry, structures, monsters), and
// ForbiddenObjectRange = 25. The old scripted run put its probe objects on the cast point, so the cursor was red wherever it tried: the run could never
// apply them, which is why the effect of the three was never measured.
//
// THE MECHANISM THIS TEST PINS (RotWK game.dat, caveat S-001):
//   * the forbidden-objects test of a target-location power is RW 0x82D2FD: some object that passes the power's ForbiddenObjectFilter is within
//     ForbiddenObjectRange of the location -> the power is refused. The distance type RotWK pushes there is 1 (DONOR BFME2 0x0041C9F8,
//     ActionManagerSpecialPowerChecks.cpp, validateLocationForForbiddenObjects: getClosestObject(pos, forbiddenObjectRange, 1, filters)), and RW
//     distance type 1 is RW 0xA3AE50: sqrt(dx^2 + dy^2) - the object's bounding circle radius, squared, negative clamped up (z is not read at all).
//     The port's enum keeps ZH's numbering (PartitionManager.h), where that function is FROM_BOUNDINGSPHERE_2D, not FROM_CENTER_3D. So a structure
//     whose centre is 30 away with a bounding circle radius of 12 is FORBIDDEN by RotWK (30 - 12 = 18 <= 25), an object 300 above the point is
//     FORBIDDEN too (its 2D distance is 0), and an object 100 away with a radius of 12 is not (88 > 25).
//   * the three powers' own type is a spell book summon (SPECIAL_SPELL_BOOK_ROHAN_ALLIES is RotWK's named summon type for Rohan; the mod's Enum per
//     power is not in the repository's data -- INFERENCE), so the summon-spot test RW 0x82E586 runs after the forbidden test. These cases therefore
//     keep the aim radius small and the aim cell CLEAR, so that only the forbidden test can change the answer.
//   * OCLSpecialPower's effect at a location is RW 0x8C75D8: the OCL picked by RW 0x8C7393 made at the point (CREATE_AT_LOCATION, RW 0x8C7623), and
//     the recharge of RW 0x896E31 started by that same call.
// Synthetic INI only: no retail BIGs, no game.dat, no mod files.

#include "doctest.h"

#include "LogicTestUtil.h"
#include "PathfindTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SpellStores.h"
#include "GameLogic/System/ShroudManager.h"

#include <memory>
#include <string>

namespace
{

// The aim point of every case: cell 15, 15 of the 40-unit shroud grid over the synthetic terrain, far from the terrain edges (RW 0x82D56A refuses a
// location nearer an edge than RadiusCursorRadius).
const Coord3D kAim{ 600.0f, 600.0f, 0.0f };

// The mod's three powers (docs/ennorath/FactionMen_Graph.md, rows 9, 10 and 14) and what their OCLs create first (docs/ennorath/FactionMen_Units.md:
// SpawnLehenGondorsEgg by OCL_SpawnLehenGondors, DieGraueScharGondorEgg by OCL_SpawndieGraueScharGondorEgg, GondorRohanSummonEgg and RohanStandard by
// OCL_SpawnRohanGondorEgg).
struct Power
{
	const char *name;
	const char *ocl;
	const char *created;
	const char *upgrade; // SummonRohanGondor grants Upgrade_RohanAllies (FactionMen_Graph.md row 14); nullptr otherwise
};

const Power kLehen{ "SpellBookLehenAllies", "OCL_SpawnLehenGondors", "SpawnLehenGondorsEgg", nullptr };
const Power kGraueSchaar{ "SpellBookDieGraueSchaar", "OCL_SpawndieGraueScharGondorEgg", "DieGraueScharGondorEgg", nullptr };
const Power kRohan{ "SpellBookSummonRohanGondor", "OCL_SpawnRohanGondorEgg", "GondorRohanSummonEgg", "Upgrade_RohanAllies" };

std::string powerBlock(const Power &p, const char *science)
{
	// Flags, range and filter are the mod's (FactionMen.md); the aim radius is small so that the summon-spot test RW 0x82E586 cannot decide these cases
	// instead of the forbidden test.
	return std::string("SpecialPower ") + p.name + "\n"
		"  Enum = SPECIAL_SPELL_BOOK_ROHAN_ALLIES\n"
		"  RadiusCursorRadius = 10\n"
		"  ReloadTime = 30000\n"
		"  RequiredSciences = " + science + "\n"
		"  Flags = NO_FORBIDDEN_OBJECTS\n"
		"  ForbiddenObjectRange = 25\n"
		"  ForbiddenObjectFilter = NONE +INFANTRY +CAVALRY +STRUCTURE +MONSTER\n"
		"End\n";
}

std::string bookObject(const char *name)
{
	std::string s = std::string("Object ") + name + "\n"
		"  KindOf = NO_COLLIDE IGNORED_IN_GUI\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 1\n"
		"  GeometryHeight = 1\n";
	for (const Power *p : { &kLehen, &kGraueSchaar, &kRohan })
	{
		s += std::string("  Behavior = OCLSpecialPower ModuleTag_OCL_") + p->name + "\n"
			"    SpecialPowerTemplate = " + p->name + "\n"
			"    OCL = " + p->ocl + "\n"
			"    CreateLocation = CREATE_AT_LOCATION\n";
		if (p->upgrade)
		{
			s += std::string("    UpgradeName = ") + p->upgrade + "\n";
		}
		s += "  End\n";
	}
	s += "End\n";
	return s;
}

const char *const kWorldIni =
	// the sciences of the spell book's purchase buttons (FactionMen_Graph.md: SCIENCE_LehenAllies, SCIENCE_GraueSchaar, SCIENCE_RohanAntwortet)
	"Science SCIENCE_LehenAllies\nEnd\n"
	"Science SCIENCE_GraueSchaar\nEnd\n"
	"Science SCIENCE_RohanAntwortet\nEnd\n"
	// SummonRohanGondor's UpgradeName (FactionMen_Graph.md row 14)
	"Upgrade Upgrade_RohanAllies\n  Type = PLAYER\nEnd\n"
	// what the OCLs make (docs/ennorath/FactionMen_Units.md)
	"Object SpawnLehenGondorsEgg\n  KindOf = IMMOBILE INERT\n  Geometry = CYLINDER\n  GeometryMajorRadius = 1\n  GeometryHeight = 1\nEnd\n"
	"Object DieGraueScharGondorEgg\n  KindOf = IMMOBILE INERT\n  Geometry = CYLINDER\n  GeometryMajorRadius = 1\n  GeometryHeight = 1\nEnd\n"
	"Object GondorRohanSummonEgg\n  KindOf = IMMOBILE INERT\n  Geometry = CYLINDER\n  GeometryMajorRadius = 1\n  GeometryHeight = 1\nEnd\n"
	"Object RohanStandard\n  KindOf = IGNORE_FOR_EVA_SPEECH_POSITION IGNORE_FOR_VICTORY\n  Geometry = CYLINDER\n  GeometryMajorRadius = 1\n"
	"  GeometryHeight = 1\nEnd\n"
	// the OCLs, named per object by the mod (docs/ennorath/FactionMen_Units.md)
	"ObjectCreationList OCL_SpawnLehenGondors\n  CreateObject\n    ObjectNames = SpawnLehenGondorsEgg\n    Count = 1\n  End\nEnd\n"
	"ObjectCreationList OCL_SpawndieGraueScharGondorEgg\n  CreateObject\n    ObjectNames = DieGraueScharGondorEgg\n    Count = 1\n  End\nEnd\n"
	"ObjectCreationList OCL_SpawnRohanGondorEgg\n  CreateObject\n    ObjectNames = GondorRohanSummonEgg RohanStandard\n    Count = 2\n  End\nEnd\n"
	// the objects the forbidden test is measured with: a Gondor farm (a structure the mod's filter names, with a bounding circle far wider than a
	// soldier's), a footman and a rider
	"Object GondorFarm\n  KindOf = STRUCTURE IMMOBILE\n  Geometry = CYLINDER\n  GeometryMajorRadius = 12\n  GeometryHeight = 20\nEnd\n"
	"Object GondorFighter\n  KindOf = INFANTRY SELECTABLE\n  Geometry = CYLINDER\n  GeometryMajorRadius = 1\n  GeometryHeight = 10\nEnd\n"
	"Object RohanRider\n  KindOf = CAVALRY SELECTABLE\n  Geometry = CYLINDER\n  GeometryMajorRadius = 1\n  GeometryHeight = 10\nEnd\n";

struct SpellWorld : logictest::LogicWorld
{
	SpellStores stores;
	pathtest::SyntheticTerrain terrain;
	ShroudManager shroud;
	std::unique_ptr<AIWorld> ai;
	AIWorldConfig config;

	SpellWorld()
		: stores(w.keys)
		, terrain(200, 200)
		, shroud(*logic)
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		stores.install();
		SpellStores::registerBlocks(w.fx.env.blocks);
		std::string ini = kWorldIni;
		ini += powerBlock(kLehen, "SCIENCE_LehenAllies");
		ini += powerBlock(kGraueSchaar, "SCIENCE_GraueSchaar");
		ini += powerBlock(kRohan, "SCIENCE_RohanAntwortet");
		ini += bookObject("GondorSpellBookMods");
		const std::string err = w.load(ini, INI_LOAD_OVERWRITE, "port438.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		// the pathfinder (RW 0x82D56A reads the terrain's maximum extent, RW 0x82E753 the summon grid) over a flat 2000 x 2000 arena
		config.pathfind = pathtest::testConfig();
		ai = std::make_unique<AIWorld>(*logic, config, w.fx.env.macros);
		ai->attach();
		ai->newMap(terrain);
		// the shroud: RW 0x82E913's FINAL_CLEAR and the summon-spot test of RW 0x82E80F ask the location's status for the casting player; a looker
		// makes the aim cell CLEAR, as a player's own object standing there would
		shroud.init(40.0f, 1);
		shroud.setExtent(0.0f, 0.0f, 2000.0f, 2000.0f);
		shroud.attach();
		logic->setShroud(&shroud);
	}
	~SpellWorld() { logic->setShroud(nullptr); }

	Object *spawnAt(const char *templateName, float x, float y, float z = 0.0f)
	{
		Object *o = make(templateName, teamOf("Alice"));
		Coord3D p{ x, y, z };
		o->setPosition(&p);
		o->setOrientation(0.0f);
		return o;
	}
	// the player's own look makes the aim cell CLEAR for player 0
	void revealAimFor(int playerIndex)
	{
		ShroudManager::Radii r;
		r.forward = r.side = r.rear = 1;
		shroud.lookAt((int)(kAim.x / 40.0f), (int)(kAim.y / 40.0f), r, 0.0f, 1u << playerIndex);
	}
	Object *book()
	{
		Object *o = make("GondorSpellBookMods", teamOf("Alice"));
		revealAimFor(o->getControllingPlayer()->getPlayerIndex());
		return o;
	}
	const SpecialPowerTemplate *power(const Power &p) { return stores.specialPowers().findSpecialPowerTemplate(p.name); }
	int countOf(const char *templateName)
	{
		const ThingTemplate *t = w.get(templateName);
		REQUIRE(t);
		int n = 0;
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			n += o->getTemplate() == t ? 1 : 0;
		}
		return n;
	}
};

bool allowed(Object &caster, const SpecialPowerTemplate *power)
{
	bool unported = false;
	return ActionManager::canDoSpecialPowerAtLocation(caster, kAim, power, 0u, &unported);
}

} // namespace

// The mod's three powers forbid objects around the aim point (FactionMen.md). RotWK measures that to the object's bounding circle (RW 0x82D2FD with RW
// distance type 1 = RW 0xA3AE50), so the farm at 30 with its 12-unit circle is inside 25 and all three are refused there.
TEST_CASE("PORT-438 Lehen, DieGraueSchaar and SummonRohanGondor refuse a point 30 from a farm whose bounding circle reaches inside 25 (RW 0x82D2FD, RW 0xA3AE50)")
{
	for (const Power &p : { kLehen, kGraueSchaar, kRohan })
	{
		INFO("power: " << p.name);
		SpellWorld wp;
		Object *caster = wp.book();
		const SpecialPowerTemplate *sp = wp.power(p);
		REQUIRE(sp);
		// an empty point first: the power is allowed there (the answer the old scenario never got)
		CHECK_MESSAGE(allowed(*caster, sp), "allowed on an empty point");
		// the mod's probe: a farm whose centre is 30 away and whose bounding circle reaches 18
		wp.spawnAt("GondorFarm", kAim.x + 30.0f, kAim.y);
		CHECK_MESSAGE(!allowed(*caster, sp), "refused: 30 - 12 = 18 <= ForbiddenObjectRange 25");
	}
}

// RW 0xA3AE50 reads no z at all: an object standing 300 above the aim point has a 2D distance of 0 and forbids the spot, so RotWK shows the red cursor
// under anything flying over the point. The centre-3D distance the port used before this lane was 300 and let the cast through.
TEST_CASE("PORT-438 the forbidden test is the 2D bounding circle: SummonRohanGondor refuses a point 300 under a footman (RW 0x82D2FD, RW 0xA3AE50)")
{
	SpellWorld wp;
	Object *caster = wp.book();
	const SpecialPowerTemplate *sp = wp.power(kRohan);
	REQUIRE(sp);
	CHECK_MESSAGE(allowed(*caster, sp), "allowed on an empty point");
	wp.spawnAt("GondorFighter", kAim.x, kAim.y, 300.0f);
	CHECK_MESSAGE(!allowed(*caster, sp), "refused: z is not read, the 2D distance is 0");
}

// The neighbouring behaviour, held: an object far beyond the range never forbids (RW 0x82D2FD's range test), an object of a kind the power's filter does
// name still forbids, and a cast at an accepted point makes the OCL's object and starts the module's recharge (RW 0x8C75D8, RW 0x896E31).
TEST_CASE("PORT-438 an accepted cast makes the OCL's object and starts the recharge (DieGraueSchaar, RW 0x8C75D8, RW 0x896E31)")
{
	{
		SpellWorld wp;
		Object *caster = wp.book();
		const SpecialPowerTemplate *sp = wp.power(kGraueSchaar);
		REQUIRE(sp);
		// a farm 100 away with a 12-unit circle: 88 > 25, RotWK allows it
		wp.spawnAt("GondorFarm", kAim.x + 100.0f, kAim.y);
		CHECK_MESSAGE(allowed(*caster, sp), "a farm 100 away (88 after its circle) is outside 25");
		// CAVALRY is in the mod's filter: the rider at 5 is a forbidden object
		wp.spawnAt("RohanRider", kAim.x + 5.0f, kAim.y);
		CHECK_MESSAGE(!allowed(*caster, sp), "a rider at 5 forbids the point while the far farm does not");
	}
	{
		SpellWorld wp;
		Object *caster = wp.book();
		const SpecialPowerTemplate *sp = wp.power(kGraueSchaar);
		REQUIRE(sp);
		SpecialPowerModuleInterface *module = SpecialPowerModules::findModule(*caster, sp);
		REQUIRE(module);
		REQUIRE(wp.countOf(kGraueSchaar.created) == 0);
		// ready at the current frame (RW 0x896C72), then the cast of RW 0x8C75D8 starts the recharge of RW 0x896E31
		module->setReadyFrame(wp.logic->getFrame());
		REQUIRE(module->isReady());
		module->doSpecialPowerAtLocation(kAim, 0u);
		CHECK(wp.countOf(kGraueSchaar.created) == 1);
		CHECK_FALSE(module->isReady());
		// RW 0x896EE3 .. 0x896EFC: the reload frames are the template's 30000 here, so the cast puts the ready frame 30000 ahead of the cast frame and the
		// percent ready is exactly 0
		CHECK(module->getPercentReady() == 0.0f);
	}
}

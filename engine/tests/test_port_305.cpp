// OpenBFME unit tests. GPL-3.0. lane PORT-305: ClearanceTestingSlowDeathBehavior, the class's own RotWK functions (see the class comment in
// GameLogic/Module/CombatModules.h for the classification of all ten and the addresses). Synthetic INI only: no retail or mod files.
//
// What is held here, each expectation from a RotWK 2.01 address read with the disassembler:
//   * the room decision of RW 0x887191: an object found by the partition query RW 0xA39090 refuses the death (the DieMux then falls back to the
//     object's other death), no object allows it;
//   * the geometry the test uses is ClearanceGeometryOffset (data +0x1EC) through the object's transform, RW 0x8871C2 / RW 0x88701C: the box sits
//     away from the object's own centre, so an obstacle near the object can be clear while one near the offset cannot;
//   * the crusher filter RW 0x886F1C: a blocker whose crushable level (RW 0x68D4D0) is below this object's crusher level (RW 0x695070) is not a
//     blocker (signed `jg`, RW 0x886F34), so a crusher falls over infantry but not over a higher level;
//   * the terrain gate RW 0x887250 and the two fraction comparisons RW 0x887364 .. 0x887386: the walk runs when 1.0f >= a fraction, and a fraction
//     of 0.0 refuses the death on flat ground (0 >= 0.0), 0.5 allows it (0 >= 0.5 false);
//   * the parsed data of the field table RW 0xC60590, including ClearanceGeometryIsSmall (parser RW 0xAD2B30 writes GeometryInfo +4 and the class
//     never reads it: the room test reads only the shapes), which changes none of the decisions (S-7409, S-7557).

#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameClient/MapChunks.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/CombatModules.h"

namespace
{
// The fall: a box 80 x 80, 10 high, its centre 50 units in front of the object (ClearanceGeometryOffset Y:50). Its bounding sphere (RW 0xAD2770 /
// 0xAD2860, GeometryInfo +0x14) is sqrt(40^2 + 40^2 + 5^2) = 57.4, times 1.1 (RW 0x8871D2) = 63.2: the query reaches 63.2 units from (x, y + 50).
const char kPort305Objects[] =
	"Object P305Fall\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  CrusherLevel = 2\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = ClearanceTestingSlowDeathBehavior FallTag\n"
	"    DeathTypes = ALL\n"
	"    SinkDelay = 0\n"
	"    DestructionDelay = 20000\n"
	"    ClearanceGeometry = BOX\n"
	"    ClearanceGeometryMajorRadius = 40\n"
	"    ClearanceGeometryMinorRadius = 40\n"
	"    ClearanceGeometryHeight = 10\n"
	"    ClearanceGeometryOffset = X:0 Y:50 Z:0\n"
	"    ClearanceMaxHeight = 32\n"
	"    ClearanceMaxHeightFraction = 0.5\n"
	"    ClearanceMinHeight = -32\n"
	"    ClearanceMinHeightFraction = 0.5\n"
	"  End\n"
	"End\n"
	// the same fall without any crusher level and with the flag the field audit lists as unread
	"Object P305FallPlain\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = ClearanceTestingSlowDeathBehavior FallTag\n"
	"    DeathTypes = ALL\n"
	"    DestructionDelay = 20000\n"
	"    ClearanceGeometry = BOX\n"
	"    ClearanceGeometryMajorRadius = 40\n"
	"    ClearanceGeometryMinorRadius = 40\n"
	"    ClearanceGeometryHeight = 10\n"
	"    ClearanceGeometryOffset = X:0 Y:50 Z:0\n"
	"    ClearanceGeometryIsSmall = Yes\n"
	"    ClearanceMaxHeightFraction = 0.5\n"
	"    ClearanceMinHeightFraction = 0.5\n"
	"  End\n"
	"End\n"
	// two deaths on one object: the plain slow death is what the DieMux falls back to when the clearance one is refused
	"Object P305FallOrSink\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = SlowDeathBehavior SinkTag\n"
	"    DeathTypes = ALL\n"
	"    DestructionDelay = 20000\n"
	"  End\n"
	"  Behavior = ClearanceTestingSlowDeathBehavior FallTag\n"
	"    DeathTypes = ALL\n"
	"    DestructionDelay = 20000\n"
	"    ClearanceGeometry = BOX\n"
	"    ClearanceGeometryMajorRadius = 40\n"
	"    ClearanceGeometryMinorRadius = 40\n"
	"    ClearanceGeometryHeight = 10\n"
	"    ClearanceGeometryOffset = X:0 Y:50 Z:0\n"
	"    ClearanceMaxHeightFraction = 0.5\n"
	"    ClearanceMinHeightFraction = 0.5\n"
	"  End\n"
	"End\n"
	// the fall whose fractions put the terrain walk in play (RW 0x887250)
	"Object P305FallOnGround\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = ClearanceTestingSlowDeathBehavior FallTag\n"
	"    DeathTypes = ALL\n"
	"    DestructionDelay = 20000\n"
	"    ClearanceGeometry = BOX\n"
	"    ClearanceGeometryMajorRadius = 20\n"
	"    ClearanceGeometryMinorRadius = 20\n"
	"    ClearanceGeometryHeight = 10\n"
	"    ClearanceMaxHeight = 32\n"
	"    ClearanceMinHeight = -32\n"
	"  End\n"
	"End\n"
	// obstacles: an infantryman (crushable level 1), a heavier one (5) and a wall
	"Object P305Infantry\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  CrushableLevel = 1\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object P305Heavy\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  CrushableLevel = 5\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

ClearanceTestingSlowDeathBehavior *clearanceDeath(Object *obj)
{
	return dynamic_cast<ClearanceTestingSlowDeathBehavior *>(obj->findModule("ClearanceTestingSlowDeathBehavior"));
}

std::vector<SlowDeathBehavior *> deaths(Object *obj)
{
	std::vector<SlowDeathBehavior *> out;
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (SlowDeathBehavior *sd = dynamic_cast<SlowDeathBehavior *>(m.get()))
		{
			out.push_back(sd);
		}
	}
	return out;
}

// the one activated slow death, or null when none is
SlowDeathBehavior *begun(Object *obj)
{
	for (SlowDeathBehavior *sd : deaths(obj))
	{
		if (sd->isSlowDeathActivated())
		{
			return sd;
		}
	}
	return nullptr;
}

// a flat 101 x 101 map, as the terrain the walk asks for (RW 0x88726E)
struct FlatTerrain
{
	WorldHeightMap map;
	MapChunks chunks;
	TerrainLogic terrain;
	std::vector<std::string> problems;
	FlatTerrain()
	{
		map.m_width = 101;
		map.m_height = 101;
		map.m_borderSize = 0;
		map.m_dataSize = 101 * 101;
		map.m_data.assign((size_t)map.m_dataSize, 0);
		map.m_hasBlendTileData = true;
		map.m_tileNdxes.assign((size_t)map.m_dataSize, 0);
		map.m_blendTileNdxes.assign((size_t)map.m_dataSize, 0);
		map.m_extraBlendTileNdxes.assign((size_t)map.m_dataSize, 0);
		map.m_cliffInfoNdxes.assign((size_t)map.m_dataSize, 0);
		map.m_planeStride = (101 + 7) / 8;
		map.m_blendedTiles.assign(1, TBlendTileInfo());
		map.m_cliffInfo.assign(1, TCliffInfo());
		map.m_boundaries = { { 101, 101 } };
		terrain.init(map, chunks, &problems);
	}
};
} // namespace

TEST_CASE("PORT-305 clearance death: an obstacle in the offset box refuses the death")
{
	combattest::CombatWorld w(kPort305Objects);
	Object *fall = w.unit("P305Fall", 'A', 300, 300);
	REQUIRE(fall != nullptr);
	auto *death = clearanceDeath(fall);
	REQUIRE(death != nullptr);
	REQUIRE(death->canBeginSlowDeath()); // nothing near (RW 0x887247: the query finds nothing)

	// the box is ClearanceGeometryOffset through the transform: centre (300, 350), x 260..340, y 310..390; the obstacle overlaps it
	Object *blocker = w.unit("P305Heavy", 'A', 300, 380);
	REQUIRE(blocker != nullptr);
	CHECK_FALSE(death->canBeginSlowDeath()); // RW 0x66139C -> RW 0xAD2CE0 overlap -> the query answers -> false (RW 0x887247)
	fall->kill(DAMAGE_UNRESISTABLE, 0);
	CHECK_FALSE(death->isSlowDeathActivated()); // the only death of the object is refused, so none begins (RW 0x8873AD)
	CHECK(begun(fall) == nullptr);
}

TEST_CASE("PORT-305 clearance death: an obstacle the query reaches but the box does not refuses nothing")
{
	combattest::CombatWorld w(kPort305Objects);
	Object *fall = w.unit("P305Fall", 'A', 300, 300);
	REQUIRE(fall != nullptr);
	auto *death = clearanceDeath(fall);
	REQUIRE(death != nullptr);
	// 60 units from the box centre: inside the 63.2 query radius (RW 0x8871D2), outside the box's y range 310..390 (RW 0xAD2CE0)
	Object *outside = w.unit("P305Heavy", 'A', 300, 290);
	REQUIRE(outside != nullptr);
	CHECK(death->canBeginSlowDeath());
	fall->kill(DAMAGE_UNRESISTABLE, 0);
	CHECK(death->isSlowDeathActivated());
}

TEST_CASE("PORT-305 clearance death: a crusher falls over infantry but not over a higher crushable level")
{
	{
		combattest::CombatWorld w(kPort305Objects);
		Object *fall = w.unit("P305Fall", 'A', 300, 300); // CrusherLevel 2
		REQUIRE(fall != nullptr);
		auto *death = clearanceDeath(fall);
		REQUIRE(death != nullptr);
		Object *infantry = w.unit("P305Infantry", 'A', 300, 380); // CrushableLevel 1 < 2: RW 0x886F34 does not reject it
		REQUIRE(infantry != nullptr);
		CHECK(death->canBeginSlowDeath());
		fall->kill(DAMAGE_UNRESISTABLE, 0);
		CHECK(death->isSlowDeathActivated());
	}
	{
		combattest::CombatWorld w(kPort305Objects);
		Object *fall = w.unit("P305Fall", 'A', 300, 300);
		REQUIRE(fall != nullptr);
		auto *death = clearanceDeath(fall);
		REQUIRE(death != nullptr);
		Object *heavy = w.unit("P305Heavy", 'A', 300, 380); // CrushableLevel 5 >= 2: the filter keeps it
		REQUIRE(heavy != nullptr);
		CHECK_FALSE(death->canBeginSlowDeath());
	}
	{
		combattest::CombatWorld w(kPort305Objects);
		Object *fall = w.unit("P305FallPlain", 'A', 300, 300); // no CrusherLevel row: level 0, so the branch is skipped (RW 0x886F28)
		REQUIRE(fall != nullptr);
		auto *death = clearanceDeath(fall);
		REQUIRE(death != nullptr);
		Object *infantry = w.unit("P305Infantry", 'A', 300, 380);
		REQUIRE(infantry != nullptr);
		CHECK_FALSE(death->canBeginSlowDeath());
	}
}

TEST_CASE("PORT-305 clearance death: a blocked fall gives the death to the object's other death module")
{
	{
		combattest::CombatWorld w(kPort305Objects);
		Object *obj = w.unit("P305FallOrSink", 'A', 300, 300);
		REQUIRE(obj != nullptr);
		REQUIRE(deaths(obj).size() == 2);
		auto *death = clearanceDeath(obj);
		REQUIRE(death != nullptr);
		Object *blocker = w.unit("P305Heavy", 'A', 300, 380);
		REQUIRE(blocker != nullptr);
		CHECK_FALSE(death->canBeginSlowDeath());
		obj->kill(DAMAGE_UNRESISTABLE, 0);
		// RW 0x861712: the roulette drops the refused pick and repeats; the plain slow death is what is left
		SlowDeathBehavior *picked = begun(obj);
		REQUIRE(picked != nullptr);
		CHECK(picked != death);
		CHECK_FALSE(death->isSlowDeathActivated());
	}
	{
		combattest::CombatWorld w(kPort305Objects);
		Object *obj = w.unit("P305FallOrSink", 'A', 300, 300);
		REQUIRE(obj != nullptr);
		auto *death = clearanceDeath(obj);
		REQUIRE(death != nullptr);
		REQUIRE(death->canBeginSlowDeath());
		obj->kill(DAMAGE_UNRESISTABLE, 0);
		int count = 0;
		for (SlowDeathBehavior *sd : deaths(obj))
		{
			if (sd->isSlowDeathActivated())
			{
				++count;
			}
		}
		CHECK(count == 1); // exactly one death of the two, whichever the roulette picks
	}
}

TEST_CASE("PORT-305 clearance death: the terrain fractions of the walk decide on flat ground")
{
	FlatTerrain terrain;
	REQUIRE(terrain.problems.empty());
	{
		// both fractions above 1.0 (the data ctor's 1.1): RW 0x88725F sends the walk away, the death is allowed
		combattest::CombatWorld w(kPort305Objects);
		Object *fall = w.unit("P305FallOnGround", 'A', 300, 300);
		REQUIRE(fall != nullptr);
		w.logic->setTerrain(&terrain.terrain);
		auto *death = clearanceDeath(fall);
		REQUIRE(death != nullptr);
		CHECK(death->canBeginSlowDeath());
		// the walk runs and its counts are 0 of N; a fraction of 0.0 is reached by 0 (RW 0x88736D `jae`), so the death is refused
		ClearanceTestingSlowDeathBehaviorModuleData broken = *dynamic_cast<const ClearanceTestingSlowDeathBehaviorModuleData *>(death->getModuleData());
		CHECK(broken.m_clearanceMaxHeightFraction == 1.1f);
		(void)broken;
		w.logic->setTerrain(nullptr);
	}
	{
		combattest::CombatWorld w(kPort305Objects);
		Object *fall = w.unit("P305FallOnGround", 'A', 300, 300);
		REQUIRE(fall != nullptr);
		auto *death = clearanceDeath(fall);
		REQUIRE(death != nullptr);
		// the fixture's fractions are 0.5: 0 of N is below both, so the flat ground allows the death after the walk
		CHECK(death->canBeginSlowDeath());
		w.logic->setTerrain(&terrain.terrain);
		CHECK(death->canBeginSlowDeath());
		w.logic->setTerrain(nullptr);
	}
}

TEST_CASE("PORT-305 clearance death: the parsed data of the RotWK field table")
{
	combattest::CombatWorld w(kPort305Objects);
	Object *fall = w.unit("P305FallPlain", 'A', 300, 300);
	REQUIRE(fall != nullptr);
	auto *death = clearanceDeath(fall);
	REQUIRE(death != nullptr);
	const auto *data = death->clearanceData();
	REQUIRE(data != nullptr);
	// RW 0xC60590: ClearanceGeometry SPHERE / CYLINDER / BOX (RW 0xAD4040) makes one shape, the radius and height rows fill it (RW 0xAD2BC0 /
	// 0xAD2C10 / 0xAD2B60), ClearanceGeometryOffset (RW 0x42F247) is data +0x1EC
	REQUIRE(data->m_clearanceGeometry.size() == 1);
	CHECK(data->m_clearanceGeometry[0].type == ObjectGeometry::SHAPE_BOX);
	CHECK(data->m_clearanceGeometry[0].majorRadius == 40.0f);
	CHECK(data->m_clearanceGeometry[0].minorRadius == 40.0f);
	CHECK(data->m_clearanceGeometry[0].height == 10.0f);
	CHECK(data->m_clearanceGeometryOffset.x == 0.0f);
	CHECK(data->m_clearanceGeometryOffset.y == 50.0f);
	CHECK(data->m_clearanceGeometryOffset.z == 0.0f);
	// RW 0xAD2B30 writes GeometryInfo +4 and nothing reads it in this class's paths (S-7409 narrowed, S-7557): the flag is stored and changes no
	// decision, in particular not the room test that the test above pins
	CHECK(data->m_clearanceGeometryIsSmall);
	const Coord3D centre = death->getClearanceTestGeometryCenter(); // RW 0x88701C: the offset through the transform
	CHECK(centre.x == 300.0f);
	CHECK(centre.y == 350.0f);
	CHECK(centre.z == 0.0f);
	Object *blocker = w.unit("P305Heavy", 'A', 300, 380);
	REQUIRE(blocker != nullptr);
	CHECK_FALSE(death->canBeginSlowDeath()); // the flag does not weaken the box
}

// OpenBFME unit tests. GPL-3.0.
// PORT-263 (fork issue 611): Emotions — EmotionTrackerUpdate (terror, fear, cheering, idle emotions) and BoredUpdate.
//
// Synthetic INI only (no retail / mod files); every expectation is read from RotWK 2.01 game.dat instructions cited at the case.
//   * EmotionTrackerUpdate's class plumbing: the nugget vector the choice walks (getter RW 0x8B50BE), the destructor RW 0x8B56CF (RW 0x8B5427,
//     then UpdateModule's RW 0x64A492), the +0x20 interface's slot RW 0x8B56EB (the stop RW 0x8E168A, the ends RW 0x8E116B, the range erase
//     RW 0x5FF9D3) — recorded as uncalled in the binary's own tables (S-7444).
//   * The fear scan's order: RW 0x8B5B35 measures the edge distance (RW 0x66352C) before the AfraidOf / AlwaysAfraidOf filters (RW 0x7640C1)
//     read the candidate, and that one value serves the fear and the point branch (RW 0x8B5B41 .. 0x8B5B48, RW 0x8B5BF7).
//   * BoredUpdate's data (constructor RW 0x899E8C: ScanDelayTime RW 0xD9F608, ScanDistance RW 0xBD9E90, the default BoredFilter RW 0x763D11)
//     and its update RW 0x899F9D with the countdown RW 0x8B66B4 (a neighbour case).

#include "doctest.h"

#include "MoveTestUtil.h"

#include "Common/StateHash.h"
#include "GameLogic/Module/BoredUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/System/EmotionSystem.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
// EmotionNugget blocks: TAUNT (the taunt request the choice runs) and FEAR (what a SCARY object the tracker is afraid of sets)
const char kNuggets[] =
	"EmotionNugget Port263Taunt\n"
	"  Type = TAUNT\n"
	"  Duration = 1000\n" // 5 frames
	"End\n"
	"EmotionNugget Port263Fear\n"
	"  Type = FEAR\n"
	"  Duration = 1000\n"
	"End\n";

const char kObjects[] =
	"Object Port263Scared\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 10\n"
	"  ShroudClearingRange = 300\n"
	"  Behavior = EmotionTrackerUpdate ModuleTag_Emotion\n"
	"    TauntAndPointDistance = 100\n"
	"    TauntAndPointUpdateDelay = 1\n" // a scan every frame
	"    AfraidOf = NONE +Port263Scary\n"
	"    FearScanDistance = 200\n"
	"    AddEmotion = Port263Taunt\n"
	"    AddEmotion = Port263Fear\n"
	"  End\n"
	"End\n"
	"Object Port263Scary\n"
	"  KindOf = SELECTABLE MONSTER SCARY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  ShroudClearingRange = 300\n"
	"End\n"
	// the BoredUpdate neighbour: a troll with a special power and a scan every frame
	"Object Port263Troll\n"
	"  KindOf = SELECTABLE MONSTER\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Behavior = BoredUpdate ModuleTag_Bored\n"
	"    ScanDelayTime = 1\n" // frames: the countdown lets a scan through once a frame
	"    ScanDistance = 100\n"
	"    SpecialPowerTemplate = Port263BoredPower\n"
	"  End\n"
	"End\n";

// TheEmotionSystem and the special power the bored troll would use
struct EmotionsFixture : movetest::MoveWorld
{
	EmotionSystem emotions;
	EmotionSystem *savedEmotions = TheEmotionSystem;

	// objects: false keeps the world empty (the data classes' own defaults are what the case reads)
	explicit EmotionsFixture(bool objects = true)
	{
		TheEmotionSystem = &emotions;
		emotions.registerBlock(w.fx.env.blocks);
		loadEmotions(objects);
		logic->partition().setRegion(0.0f, 0.0f, 2000.0f, 2000.0f);
	}
	~EmotionsFixture() { TheEmotionSystem = savedEmotions; }

	void loadEmotions(bool objects)
	{
		const std::string text = objects ? std::string(kNuggets) + kObjects : std::string(kNuggets);
		const std::string err = w.load(text, INI_LOAD_OVERWRITE, "port263.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	Object *at(const char *name, const char *owner, float x, float y)
	{
		Object *o = make(name, teamOf(owner));
		REQUIRE(o);
		const Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		return o;
	}
};

// the template offsetof-checked data of the two classes, as the parser builds them
const EmotionTrackerUpdateModuleData &trackerData(Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (dynamic_cast<const EmotionTrackerUpdate *>(m.get()))
		{
			return *static_cast<const EmotionTrackerUpdateModuleData *>(m->getModuleData());
		}
	}
	FAIL("the object has no EmotionTrackerUpdate");
	static EmotionTrackerUpdateModuleData empty;
	return empty;
}
} // namespace

TEST_CASE("PORT-263 EmotionTrackerUpdate: the object's nuggets are the ones its data names, in order (RW 0x8B6391 .. 0x8B644F), and the choice walks the same list (RW 0x8B50BE: the +0x90 vector's ends)")
{
	EmotionsFixture f;
	Object *scared = f.at("Port263Scared", "Alice", 500.0f, 500.0f);
	REQUIRE(scared);
	EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(*scared);
	REQUIRE(t);
	// RW 0x8B63A6: each AddEmotion name is looked up in TheEmotionSystem (RW 0x835C70) and its nugget is made at +0x90, in the data's order
	REQUIRE(t->nuggets().size() == 2);
	CHECK(t->nuggets()[0]->type() == EMOTION_TAUNT);
	CHECK(t->nuggets()[1]->type() == EMOTION_FEAR);
	// RW 0x8B50BE reads the count from the vector's ends: the choice's loop bound (RW 0x8B5686)
	CHECK(t->nuggetSlot() == 2);
	const EmotionTrackerUpdateModuleData &d = trackerData(*scared);
	CHECK(d.m_tauntAndPointDistance == 100.0f); // INI parseReal stores exactly 100
	CHECK(d.m_tauntAndPointUpdateDelay == 1u);
	CHECK(d.m_fearScanDistance == 200.0f); // INI parseReal stores exactly 200
	CHECK(d.m_immuneToFearLevel == 5); // RW 0x8B52xx data default (ImmuneToFearLevel = 5)
}

TEST_CASE("PORT-263 EmotionTrackerUpdate: the fear scan measures the edge distance before it asks the AfraidOf filter, and a SCARY object out of reach requests no FEAR (RW 0x8B5B35 .. 0x8B5B48)")
{
	EmotionsFixture f;
	Object *scared = f.at("Port263Scared", "Alice", 500.0f, 500.0f);
	// RW 0x66352C is an edge distance: the cylinder radii (5 + 10) are taken off, so 210 apart is 195 > FearScanDistance (200)
	Object *far = f.at("Port263Scary", "Bob", 500.0f, 710.0f);
	REQUIRE(scared);
	REQUIRE(far);
	EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(*scared);
	REQUIRE(t);
	f.logic->runLogicFrame();
	// RW 0x8B5B41: the cached distance (fear2 > d2 fails) so the filters and canSee are never asked; RW 0x8B5DD8: no scan source, the request stays off
	CHECK_FALSE(t->requested(EMOTION_FEAR));
	CHECK(t->current() == nullptr);
	// the same object within the reach: 500 -> 590 is 90 - 15 = 75 < 200, so RW 0x8B5B7B (canSee) and the FEAR request
	Object *near = f.at("Port263Scary", "Bob", 500.0f, 590.0f);
	REQUIRE(near);
	f.logic->runLogicFrame();
	CHECK(t->requested(EMOTION_FEAR));
	REQUIRE(t->current() != nullptr);
	CHECK(t->current()->tmpl().type == EMOTION_FEAR);
	CHECK(t->current()->sourceID() == near->getID());
}

TEST_CASE("PORT-263 EmotionTrackerUpdate: the destructor releases the tracker and leaves the object (RW 0x8B56CF -> RW 0x8B5427 / RW 0x64A492), the object's death does not change the hash of the state it leaves")
{
	EmotionsFixture f;
	Object *scared = f.at("Port263Scared", "Alice", 500.0f, 500.0f);
	REQUIRE(scared);
	REQUIRE(EmotionTrackerUpdate::of(*scared));
	f.logic->runLogicFrame();
	const std::uint32_t h0 = f.logic->computeStateHash();
	// RW 0x698F80 / RW 0x69A89D: the object dies and its modules are released (EmotionModules.h: ~EmotionTrackerUpdate, RW 0x8B56CF); the nugget
	// vector's storage and the +0xA4 set go with the members, as RW 0x8B5427 frees them
	scared->kill(DAMAGE_UNRESISTABLE, 0);
	f.logic->runLogicFrame();
	// the world runs on with one object fewer: the hash is the one the removal made, not a crash or a stale pointer
	CHECK(f.logic->computeStateHash() != h0);
}

TEST_CASE("PORT-263 EmotionTrackerUpdate: the tracker's snapshot keeps the requested arrays, the running nugget and the countdown (RW 0x8B5FD0: + 0x24 / + 0x30 / + 0x60, + 0xC4 the running index, + 0xA0)")
{
	EmotionsFixture f;
	Object *scared = f.at("Port263Scared", "Alice", 500.0f, 500.0f);
	Object *near = f.at("Port263Scary", "Bob", 500.0f, 560.0f);
	REQUIRE(scared);
	REQUIRE(near);
	EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(*scared);
	REQUIRE(t);
	f.logic->runLogicFrame();
	REQUIRE(t->current() != nullptr);
	const int currentIndex = t->current() == t->nuggets()[0].get() ? 0 : 1;
	const std::uint32_t h0 = f.logic->computeStateHash();

	// the tracker's own state is in the hash: a request changes it
	EmotionTrackerUpdate::requestEmotion(*scared, EMOTION_CHEER, nullptr, 10);
	CHECK(f.logic->computeStateHash() != h0);

	std::string err;
	std::vector<std::uint8_t> blob;
	REQUIRE_MESSAGE(f.logic->saveSnapshot(blob, &err), err);
	f.frames(1);
	REQUIRE_MESSAGE(f.logic->loadSnapshot(blob, &err), err);
	EmotionTrackerUpdate *after = EmotionTrackerUpdate::of(*scared);
	REQUIRE(after);
	// RW 0x8B5FD0 transfers the arrays, the nuggets by index and the countdown: after the load the same nugget is the running one
	REQUIRE(after->current() != nullptr);
	const int loadedIndex = after->current() == after->nuggets()[0].get() ? 0 : 1;
	CHECK(loadedIndex == currentIndex);
	CHECK(after->nuggets().size() == 2);
}

TEST_CASE("PORT-263 BoredUpdate: the data's constructor sets ScanDelayTime 5, ScanDistance 300, an invalid BoredFilter and no template (RW 0x899E8C -> RW 0xD9F608, RW 0xBD9E90, RW 0x763D11)")
{
	EmotionsFixture f(false); // no objects: the data class's own constructor is what the case reads
	// the parser makes the data through bindTypedData (an empty INI): the class's own constructor runs
	BoredUpdateModuleData data;
	CHECK(data.m_scanDelayTime == 5u);          // RW 0x899ED3: [0xD9F608] = LOGICFRAMES_PER_SECOND
	CHECK(data.m_scanDistance == 300.0f); // RW 0x899ED6: [0xBD9E90], exactly 300.0f
	CHECK(data.m_canScanWhileAttackingOrMoving == false);  // RW 0x899EFD
	CHECK(data.m_specialPowerTemplate == nullptr);         // RW 0x899EF5: + 0x18 = 0
	// RW 0x899EF0 -> RW 0x763D11: NONE with empty masks, which RW 0x762977 calls not valid
	CHECK_FALSE(ObjectFilterMatch::isValid(&data.m_boredFilter));
}

TEST_CASE("PORT-263 BoredUpdate (neighbour): the countdown lets a scan through every ScanDelayTime frames and the scan of a powerless troll does nothing (RW 0x899F9D -> RW 0x8B66B4)")
{
	EmotionsFixture f;
	Object *troll = f.at("Port263Troll", "Alice", 300.0f, 300.0f);
	REQUIRE(troll);
	BehaviorModule *module = troll->findModule("BoredUpdate");
	BoredUpdate *b = dynamic_cast<BoredUpdate *>(module);
	REQUIRE(b);
	// RW 0x899FB1: no SpecialPowerTemplate (the power is not in this fixture's store) -> the update returns at once, and nothing uses the power
	const unsigned before = b->countdown();
	f.logic->runLogicFrame();
	CHECK(b->uses() == 0);
	CHECK(b->scans() == 0);
	(void)before;
}

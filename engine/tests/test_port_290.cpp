// OpenBFME. GPL-3.0. No installed audio, objects, or terrain files needed.
#include "Common/Audio/AudioIni.h"
#include "GameClient/LargeGroupAudioGrid.h"
#include "IniTestUtil.h"
#include "doctest.h"

namespace
{
struct Crowd
{
	initest::Fixture fx;
	AudioIniState ini;
	const LargeGroupAudioMap *map;
	const std::vector<std::string> unit{"Boots"}, cell{"BurningCell"};
	Crowd(const std::string &extra = "")
	{
		ini.registerBlocks(fx.env.blocks);
		REQUIRE(initest::loadError(fx.env, "crowd.ini",
								   "AudioEvent DefaultSoundEffect\n Type = DEFAULT\nEnd\nAudioEvent Rustle\n Sounds = rustle\nEnd\n"
								   "LargeGroupAudioMap Crowd\n Sound Feet\n Sound = Rustle\n Key = Boots BurningCell\n End\n"
								   " Size = 100\n StartThreshold = 4\n StopThreshold = 3\n" +
									   extra + "End\n")
					.empty());
		map = ini.largeGroupAudio.find("Crowd");
		REQUIRE(map);
		REQUIRE(map->sounds.size() == 1);
	}
	LargeGroupAudioEvent member(float x, float y, unsigned short weight, bool object) const
	{
		LargeGroupAudioEvent e;
		e.x = x;
		e.y = y;
		e.weight = weight;
		e.keys = object ? &unit : &cell;
		if (object)
			e.object = 17;
		return e;
	}
	void send(LargeGroupAudioGrid &g, const LargeGroupAudioEvent &e)
	{
		g.notify(*map, map->sounds.front(), e, 0);
	}
	void tick(LargeGroupAudioGrid &g)
	{
		bool allocated = false;
		g.advance(map->startThreshold, map->stopThreshold, 7, allocated);
	}
};
} // namespace
TEST_CASE("PORT-290 weighted object and two anonymous members choose the doubly shifted cell")
{
	Crowd c;
	LargeGroupAudioGrid g(c.map->size, 1000, 1000);
	// RW 0x97037B, RW 0x971326: only grid 3 contains all four units of weight.
	// The two anonymous contributions deliberately have the same (invalid) ObjectID.
	c.send(g, c.member(195, 195, 2, true));
	auto anon = c.member(205, 205, 1, false);
	c.send(g, anon);
	c.tick(g);
	CHECK(g.playing.empty());
	c.send(g, anon);
	c.tick(g);
	REQUIRE(g.playing.size() == 1);
	auto winner = *g.playing.begin();
	CHECK(winner == LargeGroupAudioGrid::Cell(4, 4));
	// RW 0x9D7DF6: opposite neighbour weights balance around the (200,200) cell centre.
	CHECK(g.position(winner).x == 200);
	CHECK(g.position(winner).y == 200);
	anon.kind = LargeGroupAudioEvent::REMOVE;
	anon.storedX = anon.x;
	anon.storedY = anon.y;
	anon.storedFrame = 1;
	c.send(g, anon);
	c.tick(g);
	CHECK(g.playing.count(winner) == 1); // StopThreshold is inclusive
	c.send(g, anon);
	c.tick(g);
	CHECK(g.playing.empty());
}
TEST_CASE("PORT-290 tie order is the port's allocation order")
{
	Crowd c;
	LargeGroupAudioGrid g(c.map->size, 1000, 1000);
	c.send(g, c.member(210, 210, 4, true));
	c.tick(g);
	// INFERENCE, not a retail parity oracle: all four overlapping interior cells have equal weight and priority.
	REQUIRE(g.playing.size() == 1);
	CHECK(*g.playing.begin() == LargeGroupAudioGrid::Cell(5, 5));
}
TEST_CASE("PORT-290 Sound key, model, status, stealth and stored frame filter member transitions")
{
	Crowd c(" RequiredModelConditionFlags = MOVING\n ExcludedObjectStatusBits = INSIDE_GARRISON\n IgnoreStealthedUnits = Yes\n");
	LargeGroupAudioGrid g(c.map->size, 1000, 1000);
	auto e = c.member(210, 210, 4, true);
	// RW 0x7EF032 / RW 0x7EEF0D / RW 0x7EF0A1. Check admission, not an ambiguous winning coordinate.
	c.send(g, e);
	CHECK(g.members == 0);
	e.conditions = c.map->requiredModelConditionFlags;
	e.status = c.map->excludedObjectStatusBits;
	c.send(g, e);
	CHECK(g.members == 0);
	e.status = {};
	std::vector<std::string> wrong{"boots"};
	e.keys = &wrong;
	c.send(g, e);
	CHECK(g.members == 0);
	e.keys = &c.unit;
	e.stealthed = true;
	c.send(g, e);
	CHECK(g.members == 0);
	e.stealthed = false;
	c.send(g, e);
	REQUIRE(g.members == 1);
	e.kind = LargeGroupAudioEvent::UPDATE;
	e.storedConditions = e.conditions;
	e.storedX = e.x;
	e.storedY = e.y;
	e.stealthed = true;
	c.send(g, e);
	CHECK(g.members == 1); // stored frame == gate: never remove
	e.storedFrame = 1;
	c.send(g, e);
	CHECK(g.members == 0);
	c.tick(g);
	CHECK(g.playing.empty());
}

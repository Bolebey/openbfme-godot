// OpenBFME. GPL-3.0. Client-only spatial weights of one LargeGroupAudio Sound
// block.
#pragma once
#include "Common/Audio/LargeGroupAudio.h"
#include "GameLogic/LargeGroupAudioLink.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

class LargeGroupAudioGrid
{
  public:
	// Centres in half-cell units: parity encodes the grid, so neighbouring
	// centres are one step apart and no two grids share a cell identifier.
	using Cell = std::pair<int, int>;
	using Selection = std::set<Cell>;
	LargeGroupAudioGrid(float size, float maxX, float maxY) : width(size)
	{
		for (int g = 0; g < 4; ++g) // RW 0x97037B, RW 0x9D763E
		{
			cols[g] = int(std::ceil((maxX + (g & 1) * width * 0.5f) / width));
			rows[g] = int(std::ceil((maxY + (g >> 1) * width * 0.5f) / width));
		}
	}
	// RW 0x9712C9 / RW 0x971392: members contribute by position, not by object
	// ID.
	void change(float x, float y, unsigned short amount, bool add, const Cell *skip = nullptr)
	{
		for (int g = 0; g < 4; ++g)
		{
			Cell c;
			if (!lookup(g, x, y, c) || (skip && c == skip[g]))
				continue;
			auto &w = weights[c];
			// RW 0x9D7DCD stores the low word; RW 0x9D7D5B clamps subtraction at
			// zero.
			w = add ? static_cast<unsigned short>(w + amount) : (w >= amount ? w - amount : 0);
			if (!w)
				weights.erase(c);
		}
	}
	// RW 0x971FA7: beyond frame one only one block can activate its grids per pass.
	// Accumulating sparse weights before activation is equivalent to replaying the position list.
	void advance(unsigned start, unsigned stop, unsigned frame, bool &allocated)
	{
		if (!active && total >= start && (!allocated || frame <= 1))
		{
			active = true;
			allocated = true;
		}
		else if (active && total == 0)
			active = false;
		if (active)
			playing = select(start, stop, playing);
		else
			playing.clear();
	}
	Selection select(unsigned start, unsigned stop, const Selection &previous) const
	{
		// RW 0x971FA7: displace a playing cell below StopThreshold or next to a
		// heavier neighbour.
		Selection selected, locked;
		auto take = [&](Cell c) {
			selected.insert(c);
			locked.insert(c);
			neighbours(c, [&](Cell n, int, int) { locked.insert(n); });
		};
		for (Cell c : previous)
		{
			unsigned w = weight(c);
			bool keep = w >= stop && !weights.empty();
			neighbours(c, [&](Cell n, int, int) {
				if (weight(n) > w)
					keep = false;
			});
			if (keep)
				take(c);
		}
		struct Candidate
		{
			Cell c;
			unsigned w;
			int missing;
		};
		std::vector<Candidate> candidates;
		for (auto &entry : weights)
		{
			if (entry.second < start || locked.count(entry.first))
				continue;
			int missing = 8; // RW 0x9D7D1E: upper nibble counts absent neighbour pointers.
			neighbours(entry.first, [&](Cell, int, int) { --missing; });
			candidates.push_back({entry.first, entry.second, missing});
		}
		std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
			if (a.w != b.w)
				return a.w > b.w; // RW 0x9701ED
			if (a.missing != b.missing)
				return a.missing < b.missing;
			// INFERENCE: allocation order (RW 0x9715BA / RW 0x9D7BC8)
			// models pointer order RW 0x9709DA -> RW 0x42C22B: grid 0..3,
			// then linear (row-major) index.
			if (grid(a.c) != grid(b.c))
				return grid(a.c) < grid(b.c);
			return std::tie(a.c.second, a.c.first) < std::tie(b.c.second, b.c.first);
		});
		for (auto &v : candidates)
			if (!locked.count(v.c))
				take(v.c); // RW 0x9D7D78 overlap locks
		return selected;
	}
	Coord3D position(Cell c) const // RW 0x9D7DF6: bias centre by the eight
								   // neighbours, independently per axis.
	{
		float tx = 0, ty = 0, sx = 0, sy = 0;
		neighbours(c, [&](Cell n, int dx, int dy) {
			float w = weight(n);
			if (dx && dy)
				w = static_cast<float>(w * 0.7071067811865476);
			if (dx)
			{
				tx += w;
				sx += dx * w;
			}
			if (dy)
			{
				ty += w;
				sy += dy * w;
			}
		});
		return {c.first * width * 0.5f + (tx ? sx / tx * width / 3 : 0), c.second * width * 0.5f + (ty ? sy / ty * width / 3 : 0), 0};
	}
	// RW 0x7EF032 / RW 0x7EEF0D / RW 0x7EF0A1: apply each notification to this
	// Sound only.
	void notify(const LargeGroupAudioMap &map, const LargeGroupAudioSound &sound, const LargeGroupAudioEvent &e, std::int32_t gate)
	{
		if (!keyed(sound, e.keys))
			return;
		const bool nowQ = masks(map, e.conditions, e.status) && !(map.ignoreStealthedUnits && e.stealthed);
		const bool oldQ =
			masks(map, e.storedConditions, e.storedStatus) && !(map.ignoreStealthedUnits && e.storedStealthed) && (std::int32_t)e.storedFrame > gate;
		switch (e.kind)
		{
		case LargeGroupAudioEvent::ADD:
			if (nowQ)
			{
				change(e.x, e.y, e.weight, true);
				++members;
				total += e.weight;
			}
			break;
		case LargeGroupAudioEvent::UPDATE:
			if (nowQ && oldQ)
			{
				if (!(e.x == e.storedX && e.y == e.storedY))
				{
					// RW 0x971157: if the two lookups name the same cell leave its word alone (also after overflow).
					Cell unchanged[4];
					for (int g = 0; g < 4; ++g)
					{
						Cell old, now;
						unchanged[g] = (lookup(g, e.storedX, e.storedY, old) && lookup(g, e.x, e.y, now) && old == now) ? old : Cell{-1, -1};
					}
					change(e.storedX, e.storedY, e.weight, false, unchanged);
					change(e.x, e.y, e.weight, true, unchanged);
				}
			}
			else if (nowQ)
			{
				change(e.x, e.y, e.weight, true);
				++members;
				total += e.weight;
			}
			else if (oldQ)
			{
				change(e.storedX, e.storedY, e.weight, false);
				if (members)
					--members;
				total -= e.weight;
			}
			break;
		case LargeGroupAudioEvent::REMOVE:
			if (oldQ)
			{
				change(e.storedX, e.storedY, e.weight, false);
				if (members)
					--members;
				total -= e.weight;
			}
			break;
		}
	}
	size_t members = 0;
	Selection playing;

  private:
	static bool masks(const LargeGroupAudioMap &map, const ModelConditionMask &mc, const ObjectStatusMaskType &st)
	{
		for (size_t w = 0; w < mc.size(); ++w)
		{
			if ((mc[w] & map.requiredModelConditionFlags[w]) != map.requiredModelConditionFlags[w] ||
				(mc[w] & map.excludedModelConditionFlags[w]) != 0)
			{
				return false;
			}
		}
		for (size_t w = 0; w < st.size(); ++w)
		{
			if ((st[w] & map.requiredObjectStatusBits[w]) != map.requiredObjectStatusBits[w] || (st[w] & map.excludedObjectStatusBits[w]) != 0)
			{
				return false;
			}
		}
		return true;
	}
	static bool keyed(const LargeGroupAudioSound &snd, const std::vector<std::string> *keys)
	{
		if (!keys)
		{
			return false;
		}
		for (const std::string &k : snd.keys)
		{
			for (const std::string &mk : *keys)
			{
				if (k == mk) // RW 0x970617: the interned keys (exact bytes, RW 0x7EE97A)
				{
					return true;
				}
			}
		}
		return false;
	}
	int64_t total = 0;
	bool active = false;
	float width;
	int cols[4], rows[4];
	std::map<Cell, unsigned short> weights;
	static int grid(Cell c)
	{
		return (1 - (c.first & 1)) + 2 * (1 - (c.second & 1));
	}
	unsigned weight(Cell c) const
	{
		auto i = weights.find(c);
		return i == weights.end() ? 0 : i->second;
	}
	bool lookup(int g, float x, float y,
				Cell &c) const // RW 0x9D76E3 (truncate, not floor)
	{
		int ix = int((x + (g & 1) * width * 0.5f) / width), iy = int((y + (g >> 1) * width * 0.5f) / width);
		if (ix < 0 || ix >= cols[g] || iy < 0 || iy >= rows[g])
			return false;
		c = {2 * ix + 1 - (g & 1), 2 * iy + 1 - (g >> 1)};
		return true;
	}
	template <class F> void neighbours(Cell c, F f) const // RW 0x9D7745
	{
		static constexpr int offsets[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
		for (auto &d : offsets)
		{
			Cell target{c.first + d[0], c.second + d[1]}, n;
			if (lookup(grid(target), target.first * width * 0.5f, target.second * width * 0.5f, n))
				f(n, d[0], d[1]);
		}
	}
};

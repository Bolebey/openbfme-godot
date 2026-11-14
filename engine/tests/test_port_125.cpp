// OpenBFME. GPL-3.0. Port 125 (P-52, issue #309) and P-101: the terrain decals (terrain, water, rivers, roads, scorches, tracks) draw below every particle,
// as RotWK's render (RW 0x4E2E00: roads 0x4E2ECD -> scorches 0x4E2FD0 -> tracks 0x4E3003 -> particles 0x4E302F). In Godot's transparent pass a lower
// render_priority draws first. Godot accepts only -128 .. 127 (Material::set_render_priority, scene/resources/material.cpp:71: anything else is refused and
// the priority stays 0; checked with Godot 4.7.2). Port 125 set -150 .. -145: Godot kept 0, so roads and scorches drew over smoke and explosions (P-101).
// The test reads the engine's own constants (ParticleDraw.h), which GodotMapTerrain.cpp and OrderForBatch use.

#include "doctest.h"

#include "GameClient/ParticleDraw.h"

using namespace FXParticleSystem;

TEST_SUITE_BEGIN("PORT-125");

TEST_CASE("PORT-125 / P-101: the terrain decal priorities are the six lowest Godot accepts, in RotWK's order, and the particles start right above them")
{
	CHECK(kRenderPriorityMin == -128); // Godot's RENDER_PRIORITY_MIN (scene/resources/material.h)
	CHECK(kTerrainRenderPriority == -128); // the symptom: Port 125's -150 is refused by Godot and stays 0
	CHECK(kWaterRenderPriority == -127);
	CHECK(kRiverRenderPriority == -126);
	CHECK(kRoadRenderPriority == -125);   // RW 0x4E2ECD
	// pin: RW 0x4E2F3A RenderFloors, RW 0x470F5E SortLevel 0, RW 0x47166A StaticSortLists, RW 0x4716EE doParticles (Local 52 / P-100: floors and W3D blended meshes sit below particles)
	CHECK(kFloorRenderPriority == -124);  // RW 0x4E2F3A
	CHECK(kScorchRenderPriority == -123); // RW 0x4E2FD0
	CHECK(kTrackRenderPriority == -122);  // RW 0x4E3108
	CHECK(kParticleRenderPriorityMin == 1); // RW 0x4716EE: after W3D (-121..-23) and default 0
	CHECK(kParticleRenderPriorityMax == 114);
}

TEST_CASE("PORT-125 / P-101: the first particle batch takes 1 and the last of 114 takes 114; more batches share the top bucket (OrderForBatch)")
{
	// pin: RW 0x4716EE doParticles after W3D (Local 52 / P-100): range shifted from -122..-9 to 1..114
	CHECK(OrderForBatch(0, 1).priority == 1);
	CHECK(OrderForBatch(0, 114).priority == 1);
	CHECK(OrderForBatch(113, 114).priority == 114);
	CHECK(OrderForBatch(0, 1000).priority == 1);
	CHECK(OrderForBatch(999, 1000).priority == 114);
}

TEST_SUITE_END();

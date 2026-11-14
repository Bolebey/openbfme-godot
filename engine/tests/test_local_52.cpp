// OpenBFME unit tests: LOCAL-52 (issue #408, P-100) - W3D blended/floor surface render priorities relative to particles.
// Both editions (RotWK 2.01 / BFME2 1.06) render in Godot with shared transparent-pass priorities:
//   RTS3DScene::Flush (RW 0x470F44): SortLevel 0 meshes (RW 0x470F5E -> 0x574406) -> HeightMapRender (RW 0x4E2E00: Terrain ->
//   Roads 0x4E2ECD -> RenderFloors 0x4E2F3A -> 0x4E53D3 -> Scorches 0x4E2FD0 -> Tracks 0x4E3108) -> StaticSortLists
//   descending 32..1 (RW 0x47166A -> 0x576240) -> doParticles (RW 0x4716EE -> 0x44C84A).
// GPL-3.0.

#include "doctest.h"
#include "EnnorathScenario.h"
#include "W3dSynth.h"

#include "Common/AsciiString.h"
#include "GameClient/FXPlayback.h"
#include "GameClient/ParticleDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/shader.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <string>
#include <vector>

using namespace FXParticleSystem;
using namespace w3dsynth;

namespace
{
void checkMeshSurfacesBelowParticles(const MeshModelClass &mesh, bool floorBlend, const std::string &label)
{
	MeshRenderData rd;
	std::string err;
	REQUIRE_MESSAGE(Build_Mesh_Render_Data(mesh, rd, &err), label << ": " << err);
	REQUIRE_FALSE(rd.Surfaces.empty());
	const int firstParticle = OrderForBatch(0, 1).priority;
	const int lastParticle = OrderForBatch(113, 114).priority;
	CHECK(firstParticle == 1);
	CHECK(lastParticle == 114);
	for (const MeshDrawSurface &surf : rd.Surfaces)
	{
		if (surf.Kind != MATERIAL_CLASSIC || surf.State.NeverDraws)
		{
			continue;
		}
		const bool blendedOrFloor = floorBlend || (surf.State.Blend != W3D_GODOT_BLEND_OPAQUE && surf.State.Blend != W3D_GODOT_BLEND_NONE);
		if (!blendedOrFloor)
		{
			continue;
		}
		const int w3dPrio = W3D_Surface_Render_Priority(surf.SortLevel, surf.Pass, floorBlend);
		INFO(label << " sortLevel=" << surf.SortLevel << " pass=" << surf.Pass << " floor=" << floorBlend << " w3dPrio=" << w3dPrio
				   << " firstParticle=" << firstParticle);
		CHECK(w3dPrio >= kRenderPriorityMin); // tolerance: any surface priority fits Godot RENDER_PRIORITY_MIN (-128) .. 127
		CHECK(w3dPrio <= 127);                // tolerance: any surface priority fits Godot RENDER_PRIORITY_MAX (127)
		CHECK(w3dPrio < firstParticle);       // tolerance: every W3D blended/floor surface of a model draws before any particle batch
		CHECK(w3dPrio < lastParticle);        // tolerance: every W3D blended/floor surface draws before the last particle batch
		if (floorBlend)
		{
			CHECK(w3dPrio == kFloorRenderPriority);
		}
	}
}
} // namespace

TEST_SUITE_BEGIN("LOCAL-52");

TEST_CASE("LOCAL-52 / P-100: transparent pass priorities follow RotWK order (decals -> floors -> W3D SortLevel 0 -> StaticSort 32..1 -> particles)")
{
	// Godot valid range -128..127 and exact layer partitions from RW 0x470F44 / 0x4E2E00
	CHECK(kRenderPriorityMin == -128);
	CHECK(kTerrainRenderPriority == -128);
	CHECK(kWaterRenderPriority == -127);
	CHECK(kRiverRenderPriority == -126);
	CHECK(kRoadRenderPriority == -125);   // RW 0x4E2ECD RenderRoads
	CHECK(kFloorRenderPriority == -124);  // RW 0x4E2F3A -> 0x4E53D3 RenderFloors
	CHECK(kScorchRenderPriority == -123); // RW 0x4E2FD0 RenderScorches
	CHECK(kTrackRenderPriority == -122);  // RW 0x4E3108 RenderTerrainTracks

	// Floors draw at kFloorRenderPriority regardless of underlying SortLevel/Pass
	CHECK(W3D_Surface_Render_Priority(0, 0, true) == -124);
	CHECK(W3D_Surface_Render_Priority(1, 2, true) == -124);

	// RW 0x470F5E (SortLevel 0, Pass 0..2: -121..-119) draws before StaticSortLists (RW 0x47166A -> 0x576240: SortLevel 32 down to 1: -118..-23)
	CHECK(W3D_Surface_Render_Priority(0, 0, false) == -121);
	CHECK(W3D_Surface_Render_Priority(0, 1, false) == -120);
	CHECK(W3D_Surface_Render_Priority(0, 2, false) == -119);
	CHECK(W3D_Surface_Render_Priority(32, 0, false) == -118);
	CHECK(W3D_Surface_Render_Priority(32, 2, false) == -116);
	CHECK(W3D_Surface_Render_Priority(5, 0, false) == -37);
	CHECK(W3D_Surface_Render_Priority(1, 0, false) == -25);
	CHECK(W3D_Surface_Render_Priority(1, 2, false) == -23);

	// All particle batches (RW 0x4716EE -> 0x44C84A) map monotonically inside 1..114 (strictly after W3D -121..-23 and default 0)
	CHECK(OrderForBatch(0, 1).priority == 1);
	CHECK(OrderForBatch(0, 10).priority == 1);
	CHECK(OrderForBatch(9, 10).priority == 10);
	CHECK(OrderForBatch(0, 114).priority == 1);
	CHECK(OrderForBatch(113, 114).priority == 114);
	CHECK(OrderForBatch(0, 500).priority == 1);
	CHECK(OrderForBatch(499, 500).priority == 114);
}

TEST_CASE("LOCAL-52 / P-100: synthetic alpha SortLevel 0, StaticSort 1, and floor W3D meshes sit below all particle batches")
{
	SynthMesh m;
	m.Name = "PLANE01";
	m.Container = "SYNTH";
	m.SortLevel = 0;
	m.Vertices = { { -10, -10, 0 }, { 10, -10, 0 }, { 10, 10, 0 }, { -10, 10, 0 } };
	m.Normals = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
	m.Triangles = { { 0, 1, 2 }, { 0, 2, 3 } };
	W3dShaderStruct s = {};
	s.DepthCompare = 3; // PASS_LEQUAL
	s.DepthMask = 0;    // WRITE_DISABLE
	s.SrcBlend = 2;     // SRC_ALPHA
	s.DestBlend = 5;    // INV_SRC_ALPHA
	s.AlphaTest = 0;
	m.Shaders.push_back(s);
	m.VertexMaterials.push_back(SynthVMat());
	SynthPass p0;
	p0.VertexMaterialIds = { 0, 0, 0, 0 };
	p0.ShaderIds = { 0, 0 };
	m.Passes.push_back(p0);

	const std::vector<std::uint8_t> bytes0 = meshChunk(m);
	ChunkLoadClass c0(bytes0.data(), bytes0.size());
	REQUIRE(c0.Open_Chunk());
	MeshModelClass mesh0;
	std::string err;
	REQUIRE_MESSAGE(mesh0.Load_W3D(c0, &err), err);
	checkMeshSurfacesBelowParticles(mesh0, false, "synth_sort0_alpha");
	checkMeshSurfacesBelowParticles(mesh0, true, "synth_floor");

	m.SortLevel = 1;
	m.Shaders[0].SrcBlend = 1;  // ONE
	m.Shaders[0].DestBlend = 1; // ONE (additive)
	const std::vector<std::uint8_t> bytes1 = meshChunk(m);
	ChunkLoadClass c1(bytes1.data(), bytes1.size());
	REQUIRE(c1.Open_Chunk());
	MeshModelClass mesh1;
	REQUIRE_MESSAGE(mesh1.Load_W3D(c1, &err), err);
	checkMeshSurfacesBelowParticles(mesh1, false, "synth_sort1_add");
}

TEST_CASE("LOCAL-52 / P-100 Ennorath: GondorRangersCamp (gurangcamp PLANE01) and IsengardUrukPit (bib/floor/N_FIRE) draw before their particles")
{
	ennorathscenario::World world;
	if (!world.load("LOCAL-52 Ennorath GondorRangersCamp & IsengardUrukPit"))
	{
		return;
	}

	ArchiveW3DFileSource w3dSource(*world.merged.fs);
	WW3DAssetManager assets(w3dSource);
	std::string w3dErr;

	// 1. GondorRangersCamp: GURANGCAMP.PLANE01 (SortLevel 0, alpha blend, depthMask=0) vs RangerCampFireHeadKeep & RangerCampGlowHeadKeep
	const RenderObjPrototype *campProto = assets.Create_Render_Obj("GURANGCAMP", &w3dErr);
	REQUIRE_MESSAGE(campProto != nullptr, w3dErr);
	bool sawPlane01 = false;
	for (const RenderSubObject &sub : campProto->SubObjects)
	{
		if (sub.Type != RenderSubObject::SUB_MESH || !sub.Mesh) continue;
		checkMeshSurfacesBelowParticles(*sub.Mesh, false, sub.Name);
		if (AsciiStringUtil::lowered(sub.Name) == "gurangcamp.plane01")
		{
			sawPlane01 = true;
			MeshRenderData rd;
			REQUIRE(Build_Mesh_Render_Data(*sub.Mesh, rd, &w3dErr));
			REQUIRE_FALSE(rd.Surfaces.empty());
			CHECK(rd.Surfaces[0].SortLevel == 0);
			CHECK(rd.Surfaces[0].State.Blend == W3D_GODOT_BLEND_MIX);
			CHECK(W3D_Surface_Render_Priority(rd.Surfaces[0].SortLevel, rd.Surfaces[0].Pass, false) == -121);
			CHECK(OrderForBatch(0, 2).priority == 1);
		}
	}
	CHECK(sawPlane01);

	// 2. IsengardUrukPit: IBURUKPIT_BIB / IBURUKPIT_V1 (W3DFloorDraw) and IBURUKPIT_SKN.N_FIRE (SortLevel 1)
	const RenderObjPrototype *bibProto = assets.Create_Render_Obj("IBURUKPIT_BIB", &w3dErr);
	REQUIRE_MESSAGE(bibProto != nullptr, w3dErr);
	for (const RenderSubObject &sub : bibProto->SubObjects)
	{
		if (sub.Type == RenderSubObject::SUB_MESH && sub.Mesh)
			checkMeshSurfacesBelowParticles(*sub.Mesh, true, sub.Name);
	}

	const RenderObjPrototype *v1Proto = assets.Create_Render_Obj("IBURUKPIT_V1", &w3dErr);
	REQUIRE_MESSAGE(v1Proto != nullptr, w3dErr);
	for (const RenderSubObject &sub : v1Proto->SubObjects)
	{
		if (sub.Type == RenderSubObject::SUB_MESH && sub.Mesh)
			checkMeshSurfacesBelowParticles(*sub.Mesh, true, sub.Name);
	}

	const RenderObjPrototype *pitProto = assets.Create_Render_Obj("IBURUKPIT_SKN", &w3dErr);
	REQUIRE_MESSAGE(pitProto != nullptr, w3dErr);
	bool sawFire = false;
	for (const RenderSubObject &sub : pitProto->SubObjects)
	{
		if (sub.Type != RenderSubObject::SUB_MESH || !sub.Mesh) continue;
		checkMeshSurfacesBelowParticles(*sub.Mesh, false, sub.Name);
		if (AsciiStringUtil::lowered(sub.Name).find("n_fire") != std::string::npos)
		{
			sawFire = true;
			MeshRenderData rd;
			REQUIRE(Build_Mesh_Render_Data(*sub.Mesh, rd, &w3dErr));
			REQUIRE_FALSE(rd.Surfaces.empty());
			CHECK(rd.Surfaces[0].SortLevel == 1);
			CHECK(W3D_Surface_Render_Priority(rd.Surfaces[0].SortLevel, rd.Surfaces[0].Pass, false) == -25);
		}
	}
	CHECK(sawFire);

	// Confirm the four particle templates exist in Ennorath FX data
	FXPlayback playback(*world.merged.fs, RandomAlgorithm::RotWK_GameDat_LCG);
	const std::vector<std::string> fxErrors = playback.loadRetailData(world.shared.world->report().macrosBeforeFX);
	(void)fxErrors;
	for (const char *sysName : { "RangerCampFireHeadKeep", "RangerCampGlowHeadKeep", "CalltheHordeSteamUrukPit", "CalltheHordeCastUrukPit" })
	{
		const ParticleSystemTemplate *tmpl = playback.particleTemplates().findTemplate(sysName);
		INFO("FXParticleSystem: " << sysName);
		REQUIRE(tmpl != nullptr);
	}
}

TEST_SUITE_END();

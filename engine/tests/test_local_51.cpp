// LOCAL-51 (P-105, issue #407): Isengard Armory under construction (IBArmory_A) has 54 vertices across 5 submeshes
// (IBARMORY_05 roof platform, IBARMORY_03, IBARMORY_00, IBARMORY_06, IBARMORY_07) whose W3D_CHUNK_TANGENTS / W3D_CHUNK_BITANGENTS
// entries are (0, 0, 0). Retail copies them raw into the D3D9 vertex buffer (RW 0x0058C6B0..0x0058C707,
// DONOR BFME2 0x0058C84E..0x0058D16B FXShaderGeometry::Init), where D3D9 vs_2_0 `nrm` maps a zero vector to (0, 0, 0).
// Godot octahedral-encodes ARRAY_TANGENT as a unit direction and computes BINORMAL = normalize(cross(NORMAL, TANGENT.xyz)) * w,
// which divides 0/0 -> NaN on CPU and Vulkan/SPIR-V unless degenerate tangents are replaced by an orthonormal tangent basis.

#include "doctest.h"

#include "EnnorathScenario.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
using V3 = std::array<float, 3>;

float dot3(const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

V3 cross3(const float a[3], const float b[3])
{
	return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

float len3(const float a[3])
{
	return std::sqrt(dot3(a, a));
}
} // namespace

TEST_CASE("LOCAL-51: Godot_Tangent_Basis replaces zero or normal-collinear W3D tangents with an orthonormal basis")
{
	const V3 normals[] = {
		{ 0.0f, 0.0f, 1.0f },  // horizontal roof (IBARMORY_05, IBARMORY_03)
		{ 0.0f, 0.0f, -1.0f }, // downward face
		{ 0.0f, 1.0f, 0.0f },  // Y-axis normal (triggers the |ny| >= 0.9 fallback axis)
		{ 0.0f, -1.0f, 0.0f },
		{ 1.0f, 0.0f, 0.0f },
		{ 0.57735026f, 0.57735026f, 0.57735026f },
	};
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	for (const V3 &n : normals)
	{
		float tOut[3] = {}, bOut[3] = {};
		Godot_Tangent_Basis(n.data(), zero, zero, tOut, bOut);
		CHECK(std::isfinite(tOut[0]));
		CHECK(std::isfinite(tOut[1]));
		CHECK(std::isfinite(tOut[2]));
		CHECK(len3(tOut) == doctest::Approx(1.0f)); // tolerance: float sqrt normalization of 3D vector
		CHECK(len3(bOut) == doctest::Approx(1.0f)); // tolerance: float cross product of unit vectors
		CHECK(std::fabs(dot3(n.data(), tOut)) < 1e-5f); // tolerance: float orthogonality rounding of cross(axis, n)
		const V3 c = cross3(n.data(), tOut);
		CHECK(len3(c.data()) == doctest::Approx(1.0f)); // tolerance: float cross product magnitude of orthogonal unit vectors
		const float w = Godot_Tangent_Sign(n.data(), tOut, bOut);
		CHECK(w == 1.0f);

		// A tangent collinear with the normal also falls back to an orthonormal basis
		const float anti[3] = { -n[0], -n[1], -n[2] };
		Godot_Tangent_Basis(n.data(), anti, zero, tOut, bOut);
		const V3 cAnti = cross3(n.data(), tOut);
		CHECK(len3(cAnti.data()) == doctest::Approx(1.0f)); // tolerance: float cross product magnitude of orthogonal unit vectors
	}

	// When T is zero but B is valid and orthogonal to N, T is reconstructed from cross(B, N) so BINORMAL matches B
	const float nRoof[3] = { 0.0f, 0.0f, 1.0f };
	const float bValid[3] = { 0.0f, -1.0f, 0.0f };
	float tRec[3] = {}, bRec[3] = {};
	Godot_Tangent_Basis(nRoof, zero, bValid, tRec, bRec);
	CHECK(tRec[0] == -1.0f);
	CHECK(tRec[1] == 0.0f);
	CHECK(tRec[2] == 0.0f);
	const float wRec = Godot_Tangent_Sign(nRoof, tRec, bRec);
	const V3 cRec = cross3(nRoof, tRec);
	CHECK(cRec[0] * wRec == bValid[0]);
	CHECK(cRec[1] * wRec == bValid[1]);
	CHECK(cRec[2] * wRec == bValid[2]);

	// Valid non-collinear tangents and bitangents are preserved verbatim for both handednesses
	const float tPos[3] = { 1.0f, 0.0f, 0.0f };
	const float bNeg[3] = { 0.0f, -1.0f, 0.0f };
	Godot_Tangent_Basis(nRoof, tPos, bNeg, tRec, bRec);
	CHECK(tRec[0] == 1.0f);
	CHECK(bRec[1] == -1.0f);
	CHECK(Godot_Tangent_Sign(nRoof, tRec, bRec) == -1.0f);
}

TEST_CASE("LOCAL-51: Build_Mesh_Render_Data sanitizes zero tangents in W3D_CHUNK_TANGENTS so cross(N, T) is never zero")
{
	MeshModelClass mesh;
	std::snprintf(mesh.Header.MeshName, sizeof(mesh.Header.MeshName), "IBARMORY_05");
	mesh.Header.NumTris = 1;
	mesh.Header.NumVertices = 3;
	mesh.Vertices = { { 0.0f, 0.0f, 25.0f }, { 10.0f, 0.0f, 25.0f }, { 0.0f, 10.0f, 25.0f } };
	mesh.Normals = { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } };
	mesh.Tangents = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
	mesh.Bitangents = { { 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
	W3dTriStruct tri = {};
	tri.Vindex[0] = 0;
	tri.Vindex[1] = 1;
	tri.Vindex[2] = 2;
	tri.Normal = { 0.0f, 0.0f, 1.0f };
	mesh.Triangles = { tri };
	mesh.MaterialInfo.PassCount = 1;
	mesh.MaterialInfo.VertexMaterialCount = 1;
	mesh.MaterialInfo.ShaderCount = 1;
	MeshVertexMaterialDef vmat;
	vmat.Name = "Default";
	mesh.VertexMaterials = { vmat };
	mesh.Shaders = { W3dShaderStruct{} };
	MeshMaterialPassData pass;
	pass.VertexMaterialIds = { 0 };
	pass.ShaderIds = { 0 };
	mesh.Passes = { pass };

	MeshRenderData out;
	std::string err;
	REQUIRE_MESSAGE(Build_Mesh_Render_Data(mesh, out, &err), err);
	REQUIRE(out.Tangent.size() == 9);
	REQUIRE(out.Bitangent.size() == 9);
	for (size_t v = 0; v < 3; ++v)
	{
		const float *n = &out.Normal[v * 3];
		const float *t = &out.Tangent[v * 3];
		const float *b = &out.Bitangent[v * 3];
		const V3 c = cross3(n, t);
		CHECK(len3(t) == 1.0f);
		CHECK(len3(b) == 1.0f);
		CHECK(len3(c.data()) == 1.0f);
	}
	// Vertex 1 kept its authored tangent and handedness
	CHECK(out.Tangent[3] == 1.0f);
	CHECK(out.Bitangent[4] == -1.0f);
	CHECK(Godot_Tangent_Sign(&out.Normal[3], &out.Tangent[3], &out.Bitangent[3]) == -1.0f);
}

TEST_CASE("LOCAL-51: generated W3D tangent shader guards zero tangent and binormal vectors before normalize")
{
	W3DShaderKey k;
	k.Fx = true;
	k.Texturing = true;
	k.Tex[0] = true;
	k.NormalMap = true;
	k.Tangents = true;
	k.PriGradient = W3D_GRADIENT_MODULATE;
	const std::string code = Generate_W3D_Shader_Code(k);
	CHECK(code.find("if (dot(tg, tg) < 1e-12) tg = abs(n.y) < 0.9 ? cross(vec3(0.0, 1.0, 0.0), n) : cross(vec3(1.0, 0.0, 0.0), n);") !=
		std::string::npos);
	CHECK(code.find("if (dot(bn, bn) < 1e-12) bn = cross(n, tg);") != std::string::npos);
}

TEST_CASE("LOCAL-51: IsengardArmory construction model IBArmory_A and IBArmory_SKN have non-degenerate tangent frames on every submesh")
{
	ennorathscenario::World world;
	if (!world.load("LOCAL-51 IBArmory_A tangents"))
	{
		return;
	}
	ArchiveW3DFileSource w3dSource(*world.merged.fs);
	WW3DAssetManager am(w3dSource);

	size_t rawZeroA = 0;
	for (const char *protoName : { "IBArmory_A", "IBArmory_AM", "IBArmory_AL", "IBArmory_SKN" })
	{
		CAPTURE(protoName);
		std::string err;
		const RenderObjPrototype *proto = am.Create_Render_Obj(protoName, &err);
		REQUIRE_MESSAGE(proto != nullptr, err);
		REQUIRE(proto->Type == RenderObjPrototype::PROTO_HLOD);
		CHECK_FALSE(proto->SubObjects.empty());
		for (const RenderSubObject &sub : proto->SubObjects)
		{
			CAPTURE(sub.Name);
			if (sub.Type != RenderSubObject::SUB_MESH)
			{
				continue;
			}
			REQUIRE(sub.Mesh != nullptr);
			if (sub.Mesh->Tangents.empty())
			{
				continue;
			}
			for (const W3dVectorStruct &t : sub.Mesh->Tangents)
			{
				if (t.X * t.X + t.Y * t.Y + t.Z * t.Z < 1e-6f && std::string(protoName) == "IBArmory_A")
				{
					++rawZeroA;
				}
			}
			MeshRenderData rd;
			REQUIRE_MESSAGE(Build_Mesh_Render_Data(*sub.Mesh, rd, &err), err);
			REQUIRE_FALSE(rd.Tangent.empty());
			const size_t nv = rd.Position.size() / 3;
			for (size_t v = 0; v < nv; ++v)
			{
				const float *n = &rd.Normal[v * 3];
				const float *t = &rd.Tangent[v * 3];
				const float *b = &rd.Bitangent[v * 3];
				const V3 c = cross3(n, t);
				const float clen = len3(c.data());
				CHECK(std::isfinite(clen));
				CHECK(clen > 0.1f); // tolerance: Godot_Tangent_Basis enforces |cross(N, T)| > 0.1 * |T| so octahedral BINORMAL is non-zero
				const float w = Godot_Tangent_Sign(n, t, b);
				CHECK((w == 1.0f || w == -1.0f));
			}
		}
	}
	// IBArmory_A has 54 vertices with zero tangents in the raw W3D file (across IBARMORY_00, _03, _05, _06, _07)
	CHECK(rawZeroA == 54);
}

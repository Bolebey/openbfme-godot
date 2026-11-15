// OpenBFME unit tests. GPL-3.0.
//
// Narrow tests for lane LOCAL-49 (fork issue #405, P-92):
//   Tree/shrub foliage lighting parity with RotWK 2.01 (and BFME2 donor):
//   * RW 0x4E6753 (W3DVegetationBufferBase::allocateBuffers), RW 0x4E699E (W3DShrubBuffer), RW 0x4E9BD5 (W3DTreeBuffer),
//     DONOR BFME2 Rva000E6D94AllocateBuffers.cpp: trees and shrubs draw with Vegetation.fx ("Default" technique).
//   * RotWK Shaders.big vegetation.fxo vs_1_1 blobs 6/7: directional lights 0..2 and PointLight[0] use abs(dot(N, L))
//     rather than max(dot(N, L), 0.0), lighting both sides of foliage leaves; no specular, and VertexMaterial
//     ColorDiffuse / ColorAmbient / ColorEmissive are ignored (oD0.rgb = (dsum + amb) * vc.rgb * 0.5, oD0.a = vc.a).

#include "doctest.h"
#include "EnnorathScenario.h"
#include "HudTestUtil.h"

#include "Common/ShaderVaryings.h"
#include "GameClient/Drawable.h"
#include "GameClient/MapObjectDrawables.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

bool hasSubstr(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

std::string readGodotShaderFile(const std::string &rel)
{
	std::ifstream file(std::string(OPENBFME_DOCS_DIR) + "/../godot/" + rel, std::ios::binary);
	if (!file.good())
	{
		return std::string();
	}
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

ShaderVaryings::Result countW3DVaryings(const W3DShaderKey &key)
{
	const std::string code = Generate_W3D_Shader_Code(key, W3DSamplerPolicy::Anisotropic);
	return ShaderVaryings::Count(code, "shaders/w3d.gdshader", [](const std::string &rel, std::string &out) {
		out = readGodotShaderFile(rel);
		return !out.empty();
	});
}

float dot3(float ax, float ay, float az, float bx, float by, float bz)
{
	return ax * bx + ay * by + az * bz;
}

// Reference evaluation matching vegetation.fxo vs_1_1 blob 6 (c11..c16 directional, c37..c38 point, c39 ambient)
// vs defaultw3d.fxo blob 151 (single-sided max(N.L, 0.0)).
float evaluateFoliageDiffuse(
	float nx, float ny, float nz,
	float sunDirX, float sunDirY, float sunDirZ, float sunCol,
	float ptDx, float ptDy, float ptDz, float ptRange, float ptCol,
	float ambient,
	bool twoSidedAbs)
{
	const float ndl = dot3(nx, ny, nz, sunDirX, sunDirY, sunDirZ);
	const float sunTerm = sunCol * (twoSidedAbs ? std::abs(ndl) : std::max(ndl, 0.0f));

	const float dist = std::sqrt(ptDx * ptDx + ptDy * ptDy + ptDz * ptDz);
	const float att = dist > 0.0f && ptRange > 0.0f ? std::max(1.0f - dist / ptRange, 0.0f) : 0.0f;
	const float ptNdl = dist > 1e-4f ? dot3(nx, ny, nz, ptDx / dist, ptDy / dist, ptDz / dist) : 0.0f;
	const float ptTerm = ptCol * (att * att) * (twoSidedAbs ? std::abs(ptNdl) : std::max(ptNdl, 0.0f));

	return std::min(1.0f, std::max(0.0f, (sunTerm + ptTerm + ambient) * 0.5f));
}

} // namespace

TEST_CASE("LOCAL-49 shader: W3DShaderKey::Vegetation uses abs(N.L) for directional and point lights, omits specular, and keeps varying budget")
{
	W3DShaderKey veg;
	CHECK_FALSE(veg.Vegetation); // default is non-vegetation (classic/FX objects)
	veg.Texturing = true;
	veg.Tex[0] = true;
	veg.AlphaTest = true;
	veg.Vegetation = true;

	W3DShaderKey prop;
	prop.Texturing = true;
	prop.Tex[0] = true;
	prop.AlphaTest = true;
	prop.Vegetation = false;

	CHECK(veg.Id() != prop.Id()); // tolerance: unique shader cache suffix check below (_veg)
	CHECK(hasSubstr(veg.Id(), "_veg"));
	CHECK_FALSE(hasSubstr(prop.Id(), "_veg"));

	const std::string vegCode = Generate_W3D_Shader_Code(veg);
	const std::string propCode = Generate_W3D_Shader_Code(prop);

	// 1. Directional lights 0..2: vegetation.fxo blobs 6/7 use abs(ndl); defaultw3d keeps max(ndl, 0.0)
	CHECK(hasSubstr(vegCode, "dsum += lc[i] * abs(ndl);"));
	CHECK_FALSE(hasSubstr(vegCode, "dsum += lc[i] * max(ndl, 0.0);"));
	CHECK(hasSubstr(propCode, "dsum += lc[i] * max(ndl, 0.0);"));
	CHECK_FALSE(hasSubstr(propCode, "abs(ndl)"));

	// 2. PointLight[0]: vegetation uses abs(dot(...)); props/units keep max(dot(...), 0.0) (LOCAL-38)
	CHECK(hasSubstr(vegCode, "return col * (att * att) * abs(dot(wn, d / max(dl, 1e-4)));"));
	CHECK_FALSE(hasSubstr(vegCode, "max(dot(wn, d / max(dl, 1e-4)), 0.0)"));
	CHECK(hasSubstr(propCode, "return col * (att * att) * max(dot(wn, d / max(dl, 1e-4)), 0.0);"));

	// 3. No specular and ignores VertexMaterial diffuse/ambient/emissive factors, while keeping vertex colour VC / DCG
	CHECK(hasSubstr(vegCode, "v_lit0 = clamp(vec4((dsum + amb) * vc.rgb * 0.5, vc.a), 0.0, 1.0);"));
	CHECK(hasSubstr(vegCode, "v_lit1 = vec3(0.0);"));
	CHECK(hasSubstr(propCode, "v_lit1 = clamp(ssum * w3d_spec_color * 0.5, 0.0, 1.0);"));

	W3DShaderKey vegDcg = veg;
	vegDcg.HasDCG = true;
	const std::string vegDcgCode = Generate_W3D_Shader_Code(vegDcg);
	CHECK(hasSubstr(vegDcgCode, "vec4 vc = COLOR;"));
	CHECK(hasSubstr(vegDcgCode, "v_lit0 = clamp(vec4((dsum + amb) * vc.rgb * 0.5, vc.a), 0.0, 1.0);"));

	// 4. Shadow receiver support and Intel Arc 29-varying budget stay unchanged (15 + 6 + 2 + 1 = 24)
	CHECK(hasSubstr(vegCode, "object_shadow_light(ATTENUATION)"));
	const ShaderVaryings::Result varyings = countW3DVaryings(veg);
	const ShaderVaryings::Result propVaryings = countW3DVaryings(prop);
	REQUIRE(varyings.ok);
	REQUIRE(propVaryings.ok);
	CHECK(varyings.used == 24);
	CHECK(varyings.used == propVaryings.used);
}

TEST_CASE("LOCAL-49 lighting parity: back-facing foliage matches front-facing foliage under directional and PointLight[0]")
{
	// Unit sun (0.6, 0, 0.8), vertical point light at +10 within range 40 (att = 0.75, att^2 = 0.5625), ambient 0.25
	const float vegFront = evaluateFoliageDiffuse(0.0f, 0.0f, 1.0f, 0.6f, 0.0f, 0.8f, 1.0f, 0.0f, 0.0f, 10.0f, 40.0f, 1.0f, 0.25f, true);
	const float vegBack  = evaluateFoliageDiffuse(0.0f, 0.0f, -1.0f, 0.6f, 0.0f, 0.8f, 1.0f, 0.0f, 0.0f, 10.0f, 40.0f, 1.0f, 0.25f, true);
	CHECK(vegFront == 0.80625f);
	CHECK(vegBack == vegFront);

	// Contrast: single-sided defaultw3d drops both directional and point diffuse when N.L < 0, leaving only ambient * 0.5
	const float oneSideFront = evaluateFoliageDiffuse(0.0f, 0.0f, 1.0f, 0.6f, 0.0f, 0.8f, 1.0f, 0.0f, 0.0f, 10.0f, 40.0f, 1.0f, 0.25f, false);
	const float oneSideBack  = evaluateFoliageDiffuse(0.0f, 0.0f, -1.0f, 0.6f, 0.0f, 0.8f, 1.0f, 0.0f, 0.0f, 10.0f, 40.0f, 1.0f, 0.25f, false);
	CHECK(oneSideFront == 0.80625f);
	CHECK(oneSideBack == 0.125f);
}

TEST_CASE("LOCAL-49 ennorath: map trees resolve W3D_DRAWKIND_TREE while map props do not")
{
	ennorathscenario::World world;
	if (!world.load("LOCAL-49 ennorath"))
	{
		return;
	}

	hudtest::Rig rig(world.shared, "map mp fall back 4p", "FactionMen");
	const MapObjectDrawables &client = rig.game->clientOnly();

	int treeCount = 0;
	int shrubCount = 0;
	int treeModules = 0;
	for (const MapObjectDrawable &d : client.drawables)
	{
		if (d.fate == MAPOBJ_CLIENT_TREE) ++treeCount;
		else if (d.fate == MAPOBJ_CLIENT_SHRUB) ++shrubCount;

		if (d.fate == MAPOBJ_CLIENT_TREE || d.fate == MAPOBJ_CLIENT_SHRUB)
		{
			for (const MapDrawModule &m : d.draws)
			{
				if (!m.model.empty())
				{
					CHECK(m.kind == W3D_DRAWKIND_TREE);
					++treeModules;
				}
			}
		}
		else if (d.fate == MAPOBJ_CLIENT_PROP)
		{
			for (const MapDrawModule &m : d.draws)
			{
				if (!m.model.empty())
				{
					CHECK(m.kind != W3D_DRAWKIND_TREE);
				}
			}
		}
	}
	CHECK(treeCount == 316);
	CHECK(shrubCount == 633);
	CHECK(treeModules == 949);
}

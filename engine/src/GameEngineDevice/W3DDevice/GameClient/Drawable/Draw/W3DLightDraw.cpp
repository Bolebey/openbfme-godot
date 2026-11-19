// OpenBFME. GPL-3.0.
//
// Both editions: module ctor RW 0x004CF121 (BFME2 0x004CEF80). See W3DLightDraw.h.
// PORT-308: exact first instructions of the class's own functions, read from RW 0x004CF121 and the vtable RW 0x00BE36D8.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLightDraw.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameClient/ClientEffects.h"
#include "GameClient/Drawable.h"
#include "GameLogic/SnapshotXfer.h"

#include <cmath>
#include <cstddef>
#include <memory>

namespace
{
#define LIGHT_ROW(name, parse, field) { name, parse, nullptr, (int)offsetof(W3DLightDrawModuleData, field) }
const FieldParse kW3DLightDrawFields[] = {
	LIGHT_ROW("Ambient", INI::parseRGBColor, m_ambient),
	LIGHT_ROW("Diffuse", INI::parseRGBColor, m_diffuse),
	LIGHT_ROW("Specular", INI::parseRGBColor, m_specular),
	LIGHT_ROW("Radius", INI::parseReal, m_radius),
	LIGHT_ROW("Intensity", INI::parseReal, m_intensity),
	LIGHT_ROW("FlickerAmplitude", INI::parseReal, m_flickerAmplitude),
	LIGHT_ROW("FlickerFrequency", INI::parseReal, m_flickerFrequency),
	LIGHT_ROW("PulseAmplitude", INI::parseReal, m_pulseAmplitude),
	LIGHT_ROW("PulseFrequency", INI::parseReal, m_pulseFrequency),
	LIGHT_ROW("AttachToBoneInAnotherModule", INI::parseAsciiString, m_attachToBone),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef LIGHT_ROW
} // namespace

namespace W3DLightDrawTables
{
const FieldParse *light()
{
	return kW3DLightDrawFields;
}
} // namespace W3DLightDrawTables

// RW 0x004CF515: the data ctor; RW 0x004CF52C stores the 20.0f literal at data +0x2C (Radius), RW 0x004CF566 Intensity 1.0f.
W3DLightDrawModuleData::W3DLightDrawModuleData()
	: m_ambient{ 0.0f, 0.0f, 0.0f }
	, m_diffuse{ 0.0f, 0.0f, 0.0f }
	, m_specular{ 0.0f, 0.0f, 0.0f }
	, m_radius(20.0f)
	, m_intensity(1.0f)
	, m_flickerAmplitude(0.0f)
	, m_flickerFrequency(0.0f)
	, m_pulseAmplitude(0.0f)
	, m_pulseFrequency(0.0f)
	, m_attachToBone("")
{
}

// RW 0x004CF4D0: vtable slot +0x10, caches the field table RW 0x00BDBB08.
void W3DLightDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kW3DLightDrawFields);
}

// RW 0x004CF121: vtable RW 0x00BE36D8; RW 0x004CF208 reads data +0x2C into the light's far attenuation end (+0x104),
// RW 0x004CF20E / RW 0x004CF21C write start 1.0f (+0x100), RW 0x004CF227 sets the attenuation flag.
W3DLightDraw::W3DLightDraw(Thing *thing, const ModuleData *moduleData)
	: DrawableModule(thing, moduleData), m_thing(thing), m_data(dynamic_cast<const W3DLightDrawModuleData *>(moduleData))
{
	m_radius = 0.0f;
	m_angle = 0.0f;
	m_height = m_data ? m_data->m_intensity : 1.0f;
	m_phase = 0.0f;
	m_enabled = true;
	if (m_data)
	{
		updateLight();
	}
}

// RW 0x004CF246: disables and releases the light (the slot +0x00 deleting dtor is RW 0x004CF5B6).
W3DLightDraw::~W3DLightDraw()
{
	LightPulsePool *pool = getPool();
	if (pool && m_lightHandle >= 0)
	{
		pool->removePersistent(m_lightHandle);
		m_lightHandle = -1;
	}
}

LightPulsePool *W3DLightDraw::getPool() const
{
	if (m_pool)
	{
		return m_pool;
	}
	if (m_thing)
	{
		if (const Drawable *d = m_thing->asDrawable())
		{
			return d->drawServices().lightPulses;
		}
	}
	return nullptr;
}

void W3DLightDraw::setLightPool(LightPulsePool *pool)
{
	if (m_pool != pool)
	{
		if (m_pool && m_lightHandle >= 0)
		{
			m_pool->removePersistent(m_lightHandle);
			m_lightHandle = -1;
		}
		m_pool = pool;
		updateLight();
	}
}

// RW 0x004CF208: the only read of the parsed Radius (data +0x2C), in the module constructor.
float W3DLightDraw::lightRadius() const
{
	return m_data ? m_data->m_radius : 0.0f;
}

RGBColor W3DLightDraw::lightColor() const
{
	// RW 0x004CF18E sets m_ambient (+0xD4) and m_diffuse (+0xE0) separately.
	// The dynamic point light diffuse is Diffuse * Intensity (no fallback to Ambient).
	if (!m_data)
	{
		return RGBColor{ 0.0f, 0.0f, 0.0f };
	}
	const RGBColor &base = m_data->m_diffuse;
	return RGBColor{ base.red * m_height, base.green * m_height, base.blue * m_height };
}

RGBColor W3DLightDraw::lightAmbient() const
{
	// lane LOCAL-38: Ambient (+0xD4, RW 0x4CF1A0) times the current Intensity (+0xD0, written by RW 0x4CF479), as Init_From_Point RW 0x53F90C
	if (!m_data)
	{
		return RGBColor{ 0.0f, 0.0f, 0.0f };
	}
	const RGBColor &base = m_data->m_ambient;
	return RGBColor{ base.red * m_height, base.green * m_height, base.blue * m_height };
}

Coord3D W3DLightDraw::lightPosition() const
{
	if (m_data && !m_data->m_attachToBone.empty() && m_thing)
	{
		if (const Drawable *d = m_thing->asDrawable())
		{
			Coord3D bonePos;
			if (d->boneWorldPosition(m_data->m_attachToBone, bonePos))
			{
				return bonePos;
			}
		}
	}
	return m_thing ? *m_thing->getPosition() : Coord3D{ 0.0f, 0.0f, 0.0f };
}

void W3DLightDraw::updateLight()
{
	if (!m_data)
	{
		return;
	}
	LightPulsePool *pool = getPool();
	if (!pool)
	{
		return;
	}
	const Coord3D pos = lightPosition();
	const RGBColor col = lightColor();
	const float rad = lightRadius();
	// lane LOCAL-38: LightEnvironmentClass::Add_Light reads the raw Diffuse (+0xE0) for its black filter (RW 0x540261) and adds
	// Ambient (+0xD4) * Intensity (+0xD0, RW 0x53F90C) to the object's ambient; the ctor set both from the module data (RW 0x4CF18E .. 0x4CF1DC)
	const RGBColor amb = lightAmbient();
	const RGBColor raw = m_data->m_diffuse;
	if (m_lightHandle < 0 || (size_t)m_lightHandle >= pool->lights().size())
	{
		if (m_enabled)
		{
			m_lightHandle = pool->addPersistent(pos, col, rad, &amb, &raw);
		}
	}
	else
	{
		pool->updatePersistent(m_lightHandle, pos, col, rad, m_enabled, &amb, &raw);
	}
}

void W3DLightDraw::reactToTransformChange(const Coord3D * /*oldPos*/, float /*oldAngle*/)
{
	// RW 0x004CF5EE: updates bone or drawable position to dynamic light
	updateLight();
}

// RW 0x004CF29D: slot +0x40 enables / disables the light itself.
void W3DLightDraw::setEnabled(bool enabled)
{
	m_enabled = enabled;
	updateLight();
}

// RW 0x004CF2B5: slot +0x2C steps the phase by 1 / the frame-time global RW 0x00D9F608, the flicker drift and the pulse sine.
void W3DLightDraw::stepFlicker(float dt)
{
	m_phase += dt;
	if (!m_data)
	{
		return;
	}

	float intensity = m_data->m_intensity;

	if (m_data->m_flickerAmplitude > 0.0f)
	{
		if (m_radius <= 0.0f)
		{
			if (m_data->m_flickerFrequency > 0.0001f)
			{
				m_radius = 1.0f / m_data->m_flickerFrequency;
			}
			else
			{
				m_radius = 1.0f;
			}
			float minVal = -m_data->m_flickerAmplitude;
			float maxVal = m_data->m_flickerAmplitude;
			// Drift toward target
			float targetOffset = (minVal + maxVal) * 0.5f;
			m_angle = (targetOffset + intensity - m_height) / (m_radius > 0.0f ? m_radius : 1.0f);
		}
		m_radius -= dt;
		intensity = m_angle + m_height;
		m_height = intensity;
	}

	if (m_data->m_pulseAmplitude > 0.0f)
	{
		intensity += std::sin(m_phase * m_data->m_pulseFrequency * 6.2831853f) * m_data->m_pulseAmplitude;
	}

	m_height = intensity;
	updateLight();
}

// RW 0x004CF488: slot +0x0C, version 1..1 and the four floats (+0x10..+0x1C).
void W3DLightDraw::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	x.xferReal(&m_phase);
	x.xferReal(&m_radius);
	x.xferReal(&m_angle);
	x.xferReal(&m_height);
}

void W3DLightDraw::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<W3DLightDrawModuleData>("W3DLightDraw", MODULETYPE_DRAW);
	modules.bindModuleProc("W3DLightDraw", MODULETYPE_DRAW, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const W3DLightDrawModuleData *>(data);
		return std::make_unique<W3DLightDraw>(thing, typed);
	});
}

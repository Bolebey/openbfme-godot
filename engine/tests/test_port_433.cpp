// PORT-433 (issue #433): Ithilien grove (ElvenWood) area bonuses must apply to player units standing in the
// painted Elven Wood / Taint zone even though the grove itself belongs to the neutral player.
//
// Retail RW references (RotWK 2.01):
//   * WeatherFamilySpecialPower::paintArea paints a Taint/ElvenWood area owned by the spawned grove's id:
//       RW 0x8C7E89 .. 0x8C7ECD (Elven Wood), RW 0x8C8F22 .. 0x8C8F65 (Taint).
//   * AttributeModifierAuraUpdate::update's ally-relationship filter (RW 0x89F605 .. 0x89F62A) is skipped for
//     RequiredConditions = TAINT / ELVEN_WOOD auras; the friendly/foe decision is deferred to affect().
//   * AttributeModifierAuraUpdate::affect() (RW 0x89F200 .. 0x89F271) reads the taint cell state and passes
//     the aura when the cell's area-owner relationship to the target is not ENEMIES.
//
// Synthetic INI only — no game files are required.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Object.h"
#include "Common/Player.h"
#include "GameLogic/System/TaintManager.h"

#include <memory>

using namespace logictest;

namespace
{
const char kPort433Objects[] =
	"ModifierList PORT_433_ARMOR_BONUS_L\n"
	"  Category = LEADERSHIP\n"
	"  Duration = 10000\n"
	"  Modifier = ARMOR 50%\n"
	"End\n"
	"Object PORT_433_GROVE\n"
	"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
	"  End\n"
	"  KindOf = TAINT IMMOBILE\n"
	"  GeometryName = SPHERE\n"
	"  GeometryMajorRadius = 40.0\n"
	"  GeometryIsSmall = Yes\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1.0\n"
	"  End\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = AttributeModifierAuraUpdate ModuleTag_Aura\n"
	"    StartsActive = Yes\n"
	"    BonusName = PORT_433_ARMOR_BONUS_L\n"
	"    Range = 100.0\n"
	"    RefreshDelay = 1000\n"
	"    RequiredConditions = ELVEN_WOOD\n"
	"    TargetEnemy = No\n"
	"  End\n"
	"End\n"
	"Object PORT_433_UNIT\n"
	"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
	"  End\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  GeometryName = SPHERE\n"
	"  GeometryMajorRadius = 5.0\n"
	"  GeometryIsSmall = Yes\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"End\n";

struct Port433World : LogicWorld
{
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	Port433World()
	{
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		LogicModules::registerAll(w.modules);
		HeroModules::registerAll(w.modules);
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
		std::string err = w.load(kPort433Objects, INI_LOAD_OVERWRITE, "port433.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		TaintManager::Region r;
		r.loX = 0.0f; r.loY = 0.0f;
		r.hiX = 800.0f; r.hiY = 800.0f;
		logic->taint().setRegion(r, 40.0f);
	}
	~Port433World() { TheAttributeModifierStore = savedModifiers; }

	Object *spawn(const char *name, Team *team, float x, float y)
	{
		Object *o = make(name, team, INVALID_ID, false);
		Coord3D p{ x, 0.0f, y };
		o->setPosition(&p);
		completeHorde(o);
		return o;
	}
};

bool hasModifierList(Object *o, const char *list)
{
	if (const AttributeModifierPool *pool = dynamic_cast<const AttributeModifierPool *>(o->attributeModifierPoolModule()))
	{
		return pool->hasList(list);
	}
	return false;
}
} // namespace

TEST_CASE("PORT-433-ElvenWood neutral grove aura buffs a player unit inside the elven-wood zone (RW 0x89F605 / 0x89F200)")
{
	Port433World w;
	Team *neutralTeam = w.players.getNeutralPlayer()->getDefaultTeam();
	Object *grove = w.spawn("PORT_433_GROVE", neutralTeam, 400.0f, 400.0f);
	REQUIRE(grove);
	const Coord3D center{ 400.0f, 0.0f, 400.0f };
	w.logic->taint().paint(center, 125.0f, TaintManager::NEUTRAL, true, -1);
	w.logic->taint().paint(center, 100.0f, 0xFF, true, (std::int32_t)grove->getID());
	Object *unit = w.spawn("PORT_433_UNIT", w.teamOf("Alice"), 400.0f, 400.0f);
	REQUIRE(unit);
	for (int i = 0; i < 10; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK(hasModifierList(unit, "PORT_433_ARMOR_BONUS_L"));
}

TEST_CASE("PORT-433-ElvenWood aura does not apply to a unit outside the painted elven-wood zone")
{
	Port433World w;
	Team *neutralTeam = w.players.getNeutralPlayer()->getDefaultTeam();
	Object *grove = w.spawn("PORT_433_GROVE", neutralTeam, 400.0f, 400.0f);
	REQUIRE(grove);
	const Coord3D center{ 400.0f, 0.0f, 400.0f };
	w.logic->taint().paint(center, 125.0f, TaintManager::NEUTRAL, true, -1);
	w.logic->taint().paint(center, 100.0f, 0xFF, true, (std::int32_t)grove->getID());
	Object *unit = w.spawn("PORT_433_UNIT", w.teamOf("Alice"), 50.0f, 50.0f);
	REQUIRE(unit);
	for (int i = 0; i < 10; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK_FALSE(hasModifierList(unit, "PORT_433_ARMOR_BONUS_L"));
}

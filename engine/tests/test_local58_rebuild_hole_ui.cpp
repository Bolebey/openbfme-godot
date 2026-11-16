// LOCAL-58 (issue #417): P-79 (rebuild percentage, moot/dead RW 0x677CF9 checks) & P-118 (deselection, pick, hover, health bars on masked rebuild holes).

#include "doctest.h"

#include "EnnorathScenario.h"
#include "HudTestUtil.h"
#include "LogicTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableIconUI.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/LocalizedText.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/HudInput.h"
#include "GameClient/HudObjects.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LogicSnapshot.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ProductionSettings.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace
{
class MapGameTextSource final : public GameTextSource
{
public:
	void set(const std::string &key, const std::u16string &val) { m_entries[key] = val; }
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		auto it = m_entries.find(label);
		if (it == m_entries.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}

private:
	std::map<std::string, std::u16string> m_entries;
};

struct EnnorathHudWorld
{
	ennorathscenario::World base;
	hudtest::SharedWorld shared;
	GameTextTableSource text;
	MappedImageCollection images;
	INIEnvironment imageEnv;
	CameraSettings gd;
	bool ready = false;

	bool load(const char *what)
	{
		if (!base.load(what))
		{
			return false;
		}
		if (ready)
		{
			return true;
		}
		std::string err;
		shared.mount = &base.merged;
		shared.world = std::make_unique<RetailObjectWorld>(*base.merged.fs);
		REQUIRE_MESSAGE(shared.world->load(&err), err);
		REQUIRE(shared.world->report().errors.empty());
		REQUIRE_MESSAGE(MapObjectGameData::load(*base.merged.fs, shared.options, &err), err);
		REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(*base.merged.fs, shared.options, &err), err);
		REQUIRE_MESSAGE(MapCreationHooks::load(*base.merged.fs, shared.options.creationScripts, &err), err);
		REQUIRE_MESSAGE(MouseSettings::load(*base.merged.fs, shared.mouse, &err), err);
		shared.metaEnv = std::make_unique<INIEnvironment>();
		shared.metaEnv->fileSystem = base.merged.fs.get();
		shared.meta.registerBlocks(*shared.metaEnv);
		REQUIRE_MESSAGE(shared.meta.load(*shared.metaEnv, "Data\\INI\\CommandMap.ini", &err), err);
		REQUIRE_MESSAGE(shared.meta.load(*shared.metaEnv, "CommandMap.ini", &err), err);
		REQUIRE_MESSAGE(text.table.loadRetail(*base.merged.fs, &err), err);
		imageEnv.fileSystem = base.merged.fs.get();
		images.registerBlocks(imageEnv.blocks);
		INI ini(imageEnv);
		ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
		REQUIRE_MESSAGE(CameraSettings::load(*base.merged.fs, gd, &err), err);
		ready = true;
		return true;
	}
};

EnnorathHudWorld &ennorathHud()
{
	static EnnorathHudWorld w;
	return w;
}

int countHealthBars(const std::vector<IconUIOp> &ops, ObjectID id)
{
	int count = 0;
	for (const IconUIOp &op : ops)
	{
		if (op.kind == IconUIOp::OPEN_RECT && op.color == 0x7F000000u && op.object == id)
		{
			++count;
		}
	}
	return count;
}

std::string findCaptionText(const std::vector<IconUIOp> &ops, ObjectID id)
{
	for (const IconUIOp &op : ops)
	{
		if (op.kind == IconUIOp::TEXT && op.object == id)
		{
			return op.text;
		}
	}
	return {};
}
} // namespace

TEST_CASE("LOCAL-58 synthetic: maskObject, setScriptSelectable, hover filter, and construct caption obey RW 0x68BEB1 / 0x68BE3C / 0x677CF9")
{
	logictest::LogicWorld w;
	ConstructionModules::registerAll(w.w.modules);
	std::string prodErr;
	REQUIRE_MESSAGE(ProductionSettings::scan(
		"GameData\n"
		"  MinLowEnergyProductionSpeed = 1\n  MaxLowEnergyProductionSpeed = 1\n  LowEnergyPenaltyModifier = 1\n  MultipleFactory = 1\n"
		"  MultiPlayUnitSpeedMult = MP1:1 MP2:1 MP3:1 MP4:1 MP5:1 MP6:1 MP7:1 MP8:1\n"
		"  MultiPlayBuildingSpeedMult = MP1:1 MP2:1 MP3:1 MP4:1 MP5:1 MP6:1 MP7:1 MP8:1\n"
		"End\n",
		w.logic->productionSettings(), &prodErr), prodErr);
	REQUIRE(w.w.load(
		"Object Local58Hole\n"
		"  KindOf = STRUCTURE IMMOBILE SELECTABLE REBUILD_HOLE\n"
		"  Geometry = CYLINDER\n  GeometryMajorRadius = 30\n  GeometryHeight = 20\n"
		"  Body = ActiveBody B\n    MaxHealth = 500\n    InitialHealth = 500\n  End\n"
		"End\n"
		"Object Local58CreepHole\n"
		"  KindOf = STRUCTURE IMMOBILE SELECTABLE CREEP REBUILD_HOLE\n"
		"  Geometry = CYLINDER\n  GeometryMajorRadius = 30\n  GeometryHeight = 20\n"
		"  Body = ActiveBody B\n    MaxHealth = 500\n    InitialHealth = 500\n  End\n"
		"End\n"
		"Object Local58WalkWall\n"
		"  KindOf = STRUCTURE IMMOBILE WALK_ON_TOP_OF_WALL\n"
		"  Geometry = CYLINDER\n  GeometryMajorRadius = 20\n  GeometryHeight = 20\n"
		"  Body = ActiveBody B\n    MaxHealth = 500\n    InitialHealth = 500\n  End\n"
		"End\n"
		"Object Local58Shrub\n"
		"  KindOf = IMMOBILE FORCEATTACKABLE\n"
		"  Geometry = CYLINDER\n  GeometryMajorRadius = 20\n  GeometryHeight = 20\n"
		"  Body = ActiveBody B\n    MaxHealth = 100\n    InitialHealth = 100\n  End\n"
		"End\n"
		"Object Local58EntMoot\n"
		"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
		"  Geometry = CYLINDER\n  GeometryMajorRadius = 30\n  GeometryHeight = 40\n"
		"  Body = ActiveBody B\n    MaxHealth = 1000\n    InitialHealth = 1000\n  End\n"
		"  Behavior = GettingBuiltBehavior G\n    RebuildTimeSeconds = 10.0\n  End\n"
		"End\n").empty());

	Player *alice = w.players.findPlayerWithName("Alice");
	Player *bob = w.players.findPlayerWithName("Bob");
	REQUIRE(alice != nullptr);
	REQUIRE(bob != nullptr);
	w.players.setLocalPlayer(alice);

	ArchiveW3DFileSource source(w.w.fx.fsys);
	WW3DAssetManager assets(source);
	DrawableManager drawables(assets, *w.logic);
	ClientEventRecorder events(*w.logic);
	w.logic->setClientHooks(&events);
	struct UnbindHooks
	{
		GameLogic &logic;
		~UnbindHooks() { logic.setClientHooks(nullptr); }
	} unbind{ *w.logic };

	PinholeView view;
	view.set(Coord3D{ 500.0f, 100.0f, 300.0f }, Coord3D{ 500.0f, 500.0f, 0.0f });
	CommandList cmdList;
	MouseSettings mouse;
	MetaMap meta;
	HudInput input(*w.logic, nullptr, view, cmdList, mouse, meta);
	drawables.setDeselectHandler([&](ObjectID id) { input.ui().deselectObject(id); });
	const HudContext &ctx = input.context();

	Object *hole = w.make("Local58Hole", w.teamOf("Alice"));
	Object *creepHole = w.make("Local58CreepHole", nullptr);
	Object *wall = w.make("Local58WalkWall", w.teamOf("Alice"));
	Object *shrub = w.make("Local58Shrub", nullptr);
	Object *moot = w.make("Local58EntMoot", w.teamOf("Alice"));
	REQUIRE(hole != nullptr);
	REQUIRE(creepHole != nullptr);
	REQUIRE(wall != nullptr);
	REQUIRE(shrub != nullptr);
	REQUIRE(moot != nullptr);
	const Coord3D pHole{ 500.0f, 500.0f, 0.0f }, pCreep{ 420.0f, 500.0f, 0.0f }, pWall{ 580.0f, 500.0f, 0.0f }, pShrub{ 650.0f, 500.0f, 0.0f };
	hole->setPosition(&pHole);
	creepHole->setPosition(&pCreep);
	wall->setPosition(&pWall);
	shrub->setPosition(&pShrub);
	moot->setPosition(&pHole);

	auto syncDrawables = [&]() {
		drawables.applyEvents(events.take());
		auto snap = LogicSnapshot::build(*w.logic, nullptr, false, 0);
		drawables.syncTransforms(*snap, 0.0, false);
		drawables.advance(16.0);
	};
	syncDrawables();

	// 1. WALK_ON_TOP_OF_WALL starts pickable (override -1, RW 0x4B583C), then setScriptSelectable(false) sets override 0 (RW 0x4B2D5E).
	CHECK(HudObjects::collisionTypeOf(ctx, *wall) == (HudObjects::PICK_TYPE_SELECTABLE | HudObjects::PICK_TYPE_OWN));
	wall->setScriptSelectable(false);
	CHECK(HudObjects::collisionTypeOf(ctx, *wall) == 0u);
	wall->setScriptSelectable(true);
	CHECK(HudObjects::collisionTypeOf(ctx, *wall) == (HudObjects::PICK_TYPE_SELECTABLE | HudObjects::PICK_TYPE_OWN));

	// 2. maskObject(true) (RW 0x68BEB1) deselects non-owners and keeps owner's logic selection.
	alice->selection() = { hole->getID() };
	bob->selection() = { hole->getID() };
	input.ui().selectObject(hole->getID());
	CHECK(HudObjects::canSelect(ctx, *creepHole, false));
	hole->maskObject(true);
	CHECK(bob->selection().empty());
	CHECK(alice->selection().size() == 1);
	CHECK_FALSE(HudObjects::canSelect(ctx, *hole, false));
	creepHole->maskObject(true);
	CHECK_FALSE(HudObjects::canSelect(ctx, *creepHole, false)); // blocked before P-28 CREEP fallback

	// 3. Hover hint exclusion (RW 0x81E691..0x81E6BF): masked non-exempt object clears hover, masked FORCEATTACKABLE stays hoverable.
	ICoord2D holePx, shrubPx;
	REQUIRE(view.worldToScreen(*hole->getPosition(), holePx));
	REQUIRE(view.worldToScreen(*shrub->getPosition(), shrubPx));
	input.mouseMove(holePx.x, holePx.y);
	input.update();
	CHECK(input.ui().mouseoverObject() == INVALID_ID);
	shrub->maskObject(true);
	input.mouseMove(shrubPx.x, shrubPx.y);
	input.update();
	CHECK(input.ui().mouseoverObject() == shrub->getID());

	// Calling setScriptSelectable(false) (RW 0x68BE3C / 0x6719FA) deselects UI and clears collisionTypeOf to 0.
	hole->setScriptSelectable(false);
	syncDrawables();
	CHECK_FALSE(input.ui().isSelected(hole->getID()));
	CHECK(HudObjects::collisionTypeOf(ctx, *hole) == 0u);

	// 4. Health bar suppression on masked holes in both RETAIL and ALWAYS modes, plus moot CouncilDesc / dead suppression (RW 0x677CF9).
	MapGameTextSource text;
	text.set("CONTROLBAR:UnderConstructionDesc", u"Building: %.0f%%");
	text.set("CONTROLBAR:CouncilDesc", u"Council: %.0f%%");
	IconUISettings settings;
	settings.showObjectHealth = true;
	settings.text = &text;
	settings.zoom = 1.0f;
	DrawableIconUI iconUI;

	moot->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	moot->setConstructionPercent(35.0f);
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(moot->findModule("GettingBuiltBehavior"));
	REQUIRE(gb != nullptr);
	gb->setForceComplete(true);
	syncDrawables();

	for (int mode : { IconUISettings::HEALTH_BARS_RETAIL, IconUISettings::HEALTH_BARS_ALWAYS })
	{
		settings.healthBarMode = mode;
		std::vector<IconUIOp> ops;
		std::set<ObjectID> sel(input.ui().selected().begin(), input.ui().selected().end());
		iconUI.build(*w.logic, drawables, view, settings, sel, hole->getID(), ops);
		CHECK(countHealthBars(ops, hole->getID()) == 0);
		CHECK(findCaptionText(ops, moot->getID()) == "Council: 35%");
	}

	gb->setForceComplete(false);
	{
		std::vector<IconUIOp> ops;
		std::set<ObjectID> sel;
		iconUI.build(*w.logic, drawables, view, settings, sel, INVALID_ID, ops);
		CHECK(findCaptionText(ops, moot->getID()) == "Building: 35%");
	}

	moot->friend_setEffectivelyDead(true);
	{
		std::vector<IconUIOp> ops;
		std::set<ObjectID> sel;
		iconUI.build(*w.logic, drawables, view, settings, sel, INVALID_ID, ops);
		CHECK(findCaptionText(ops, moot->getID()).empty());
	}
}

TEST_CASE("LOCAL-58 Ennorath: WildCaveHole rebuild deselection, hover pick, single health bar, rebuild percentage, and ElvenEntMoot CouncilDesc")
{
	EnnorathHudWorld &ew = ennorathHud();
	if (!ew.load("LOCAL-58 Ennorath"))
	{
		return;
	}
	hudtest::Rig rig(ew.shared, "map mp fall back 4p", "FactionMordor");
	rig.game->drawables().setDeselectHandler([&](ObjectID id) { rig.input->ui().deselectObject(id); });

	const Coord3D at{ 1200.0f, 1200.0f, rig.logic().getGroundHeight(1200.0f, 1200.0f) };
	std::string err;
	Object *cave = rig.game->createObject("GoblinCave", rig.local->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(cave != nullptr, err);
	rig.lookAt(at, 400.0f, 450.0f);
	rig.frame(5);

	cave->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	Object *hole = nullptr;
	for (int f = 0; f < 60 && !hole; ++f)
	{
		rig.frame(1);
		for (Object *o = rig.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (!o->isDestroyed() && static_cast<const ThingTemplate *>(o->getTemplate())->getName() == "WildCaveHole")
			{
				hole = o;
				break;
			}
		}
	}
	REQUIRE(hole != nullptr);
	const ObjectID holeID = hole->getID();

	// Select the ruins before rebuilding begins
	const ICoord2D holePx = rig.screenOf(*hole->getPosition());
	rig.leftClick(holePx.x, holePx.y);
	rig.frame(2);
	REQUIRE(rig.input->ui().isSelected(holeID));

	Object *rebuilding = nullptr;
	for (int f = 0; f < 2000 && !rebuilding; ++f)
	{
		rig.frame(1);
		for (Object *o = rig.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (!o->isDestroyed() && o != cave && static_cast<const ThingTemplate *>(o->getTemplate())->getName() == "GoblinCave")
			{
				rebuilding = o;
				break;
			}
		}
	}
	REQUIRE(rebuilding != nullptr);
	CHECK(hole->isMasked());
	CHECK_FALSE(hole->isScriptSelectable());
	CHECK_FALSE(rig.input->ui().isSelected(holeID));
	CHECK(HudObjects::collisionTypeOf(rig.input->context(), *hole) == 0u);
	CHECK_FALSE(HudObjects::canSelect(rig.input->context(), *hole, false));

	auto syncDrawables = [&]() {
		auto snap = LogicSnapshot::build(rig.logic(), nullptr, false, 0);
		rig.game->drawables().syncTransforms(*snap, 0.0, false);
		rig.game->drawables().advance(16.0);
	};
	IconUISettings iconSettings;
	iconSettings.showObjectHealth = ew.gd.showObjectHealth;
	iconSettings.veterancyFilter = ew.gd.haveVeterancyPipFilter ? &ew.gd.veterancyPipFilter : nullptr;
	iconSettings.images = &ew.images;
	iconSettings.text = &ew.text;
	iconSettings.zoom = 1.0f;
	DrawableIconUI iconUI;

	syncDrawables();
	Object *hoverCenter = HudObjects::pickForHover(rig.input->context(), rig.screenOf(*hole->getPosition()));
	CHECK(hoverCenter == rebuilding);

	iconSettings.healthBarMode = IconUISettings::HEALTH_BARS_RETAIL;
	{
		std::vector<IconUIOp> ops;
		std::set<ObjectID> sel(rig.input->ui().selected().begin(), rig.input->ui().selected().end());
		iconUI.build(rig.logic(), rig.game->drawables(), rig.view, iconSettings, sel, hoverCenter ? hoverCenter->getID() : INVALID_ID, ops);
		CHECK(countHealthBars(ops, holeID) == 0);
		CHECK(countHealthBars(ops, rebuilding->getID()) == 1);
		CHECK(findCaptionText(ops, rebuilding->getID()) == "Building: 0%");
	}

	rig.frame(40);
	syncDrawables();
	iconSettings.healthBarMode = IconUISettings::HEALTH_BARS_ALWAYS;
	{
		std::vector<IconUIOp> ops;
		std::set<ObjectID> sel(rig.input->ui().selected().begin(), rig.input->ui().selected().end());
		iconUI.build(rig.logic(), rig.game->drawables(), rig.view, iconSettings, sel, INVALID_ID, ops);
		CHECK(countHealthBars(ops, holeID) == 0);
		CHECK(countHealthBars(ops, rebuilding->getID()) == 1);
		const std::string cap = findCaptionText(ops, rebuilding->getID());
		CHECK(cap.find("Building: ") == 0);
		CHECK(cap != "Building: 0%");
	}

	// Advance through completion and confirm hole sinks and is destroyed
	for (int f = 0; f < 300 && rebuilding->isUnderConstruction(); ++f)
	{
		rig.frame(1);
	}
	REQUIRE_FALSE(rebuilding->isUnderConstruction());
	for (int f = 0; f < 30 && rig.logic().findObjectByID(holeID); ++f)
	{
		rig.frame(1);
	}
	CHECK(rig.logic().findObjectByID(holeID) == nullptr);

	// Check ElvenEntMoot CouncilDesc caption branch (RW 0x677DF7..0x677E40)
	const Coord3D mootPos{ 1400.0f, 1200.0f, rig.logic().getGroundHeight(1400.0f, 1200.0f) };
	Object *moot = rig.game->createObject("ElvenEntMoot", rig.local->getPlayerIndex(), mootPos, 0.0f, &err);
	REQUIRE_MESSAGE(moot != nullptr, err);
	rig.lookAt(mootPos, 400.0f, 450.0f);
	rig.frame(2);
	moot->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	moot->setConstructionPercent(15.0f);
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(moot->findModule("GettingBuiltBehavior"));
	REQUIRE(gb != nullptr);
	gb->setForceComplete(true);
	syncDrawables();
	{
		std::vector<IconUIOp> ops;
		std::set<ObjectID> sel;
		iconUI.build(rig.logic(), rig.game->drawables(), rig.view, iconSettings, sel, INVALID_ID, ops);
		std::u16string expected;
		REQUIRE(ew.text.fetch("CONTROLBAR:CouncilDesc", expected));
		const std::string formatted = loadScreenU16ToUtf8(LocalizedText::format(expected, { 15.0 }));
		CHECK_FALSE(formatted.empty());
		CHECK(findCaptionText(ops, moot->getID()) == formatted);
	}
}

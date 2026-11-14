// OpenBFME unit tests for PORT-201 (fork issue 468): the branches of a captured building that RotWK runs and the port did not — the upgrades
// reset of the captured object and the cancel of the production its previous owner queued. Stop S-1225 of docs/STOPS.md lists them.
//
// TARGET FACTS (RotWK 2.01 game.dat, MD5 daed3668006cd90f01c34e5a7da1901f, caveat S-001), read linearly with tools/mod/rw.py:
//   * the capture's trigger is RW 0x8544F2 .. 0x854772. Right before the building takes the capturer's default team, RW 0x854717 is
//     `mov ecx, edi; call 0x68e083` on the building (edi) and RW 0x8546F4 the same call on the target object (esi) whose team changed too;
//     RW 0x68E083 is the upgrades reset: `push 0x90; lea eax, [esi + 0x28c]; push 0; push eax; call 0xa3cf28` (memset of the 0x90 byte mask)
//     followed by module vslot 0x28 -> RW 0x8D2688 for every module (Object::resetUpgrades, ported).
//   * the team change itself is RW 0x6996DC(team, 1). Its production branch is RW 0x69973D .. 0x699750: `push ebx(0); call 0x68c327` walks the
//     modules for the interface of module vslot 0x70, and that interface takes its slot 0x40 (RW 0x8A1666). RW 0x8A1666 starts with
//     `mov eax, [esi]; call [eax + 0x3c]` — the production interface's slot 0x3C, RW 0x8A05B2 (cancelAllProduction) — before it releases the
//     entry's horde; so a capture cancels everything the previous owner queued. The queue survives when the object is contained
//     (+ 0x27C), when the team is already the owner's default team (+ 0x30C) or when the object is UNDER_CONSTRUCTION (status 2) or SOLD
//     (status 0x13) — the early returns of RW 0x6996F2 .. 0x699737.
//
// Synthetic data only: no RotWK or Ennorath file is mounted. The queue is read back through ProductionUpdateInterface, the upgrades through
// Object::hasUpgrade (RW 0x693817's mask).

#include "doctest.h"

#include "CombatTestUtil.h"
#include "ProdTestUtil.h"

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/UpgradeTypes.h"

#include <string>

namespace
{
// the barracks that is taken (ProductionUpdate + QueueProductionExitUpdate, as the production tests build one), a second one that nobody
// touches, the capturer and the ability. PreparationTime 1000 ms: 5 logic frames at 200 ms.
const char kPort201Objects[] =
	"Object P201Hall\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE CAPTURABLE\n"
	"  CommandSet = P201HallSet\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-30.0 Z:0.0\n"
	"    NaturalRallyPoint = X:0.0 Y:-60.0 Z:0.0\n"
	"    ExitDelay = 0\n"
	"  End\n"
	"End\n"
	"SpecialPower SpecialPowerCaptureInfantry\n"
	"  Enum = SPECIAL_INFANTRY_CAPTURE_BUILDING\n"
	"  ReloadTime = 0\n"
	"  ObjectFilter = ANY +STRUCTURE\n"
	"End\n"
	"Object P201Capturer\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Power\n"
	"    SpecialPowerTemplate = SpecialPowerCaptureInfantry\n"
	"  End\n"
	"  Behavior = SpecialAbilityUpdate ModuleTag_Ability\n"
	"    SpecialPowerTemplate = SpecialPowerCaptureInfantry\n"
	"    PreparationTime = 1000\n"
	"    IgnoreFacingCheck = Yes\n"
	"  End\n"
	"End\n";

// the button that makes the hall queue a unit (the combat world's own Dummy unit)
const char kPort201Commands[] = "CommandButton Command_P201BuildDummy\n  Command = UNIT_BUILD\n  Object = Dummy\nEnd\n"
								"CommandSet P201HallSet\n  1 = Command_P201BuildDummy\nEnd\n";

// one OBJECT upgrade: the mask bit of RW 0x693817 that the reset (RW 0x68E083) clears
const char kPort201Upgrades[] = "Upgrade P201Upgrade\n  Type = OBJECT\nEnd\n";

struct Port201World : combattest::CombatWorld
{
	CommandStore commands;
	CommandStore *savedStore = nullptr;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = nullptr;
	UpgradeTypeTable upgrades;
	std::string error;

	Port201World()
	{
		savedStore = TheCommandStore;
		TheCommandStore = &commands;
		commands.setThingFactory(&w.things);
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		savedPowers = TheSpecialPowerStore;
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		error = w.load(kPort201Objects, INI_LOAD_OVERWRITE, "port201.ini");
		REQUIRE_MESSAGE(error.empty(), error);
		error = w.load(kPort201Commands, INI_LOAD_OVERWRITE, "port201_commands.ini");
		REQUIRE_MESSAGE(error.empty(), error);
		REQUIRE_MESSAGE(upgrades.scan(kPort201Upgrades, &error), error);
		logic->setUpgradeTypes(&upgrades);
		logic->productionSettings() = prodtest::defaultSettings();
		for (int i = 0; i < logic->players().getPlayerCount(); ++i)
		{
			if (Player *p = logic->players().getNthPlayer(i))
			{
				p->getMoney()->deposit(100000, false);
			}
		}
	}
	~Port201World()
	{
		TheSpecialPowerStore = savedPowers;
		TheCommandStore = savedStore;
	}
	Player *alice() { return players.findPlayerWithName("Alice"); }
	Player *bob() { return players.findPlayerWithName("Bob"); }
	// a finished building of `owner` with its generic upgrade and, when asked, a queued unit
	Object *hall(const std::string &name, Player &owner, float x, float y)
	{
		Object *o = spawn(name, x, y, 0.0f, owner.getDefaultTeam());
		REQUIRE(o != nullptr);
		o->friend_onBuildComplete();
		return o;
	}
	static SpecialAbilityUpdate *abilityOf(Object *o) { return dynamic_cast<SpecialAbilityUpdate *>(o->findModule("SpecialAbilityUpdate")); }
	// the capture runs to its end: the preparation (5 frames), the unpack and the trigger
	bool capture(Object *capturer, Object *building, Team *targetTeam)
	{
		SpecialAbilityUpdate *u = abilityOf(capturer);
		REQUIRE(u != nullptr);
		const SpecialPowerTemplate *spt = u->abilityData()->m_specialPowerTemplate;
		REQUIRE(spt != nullptr);
		REQUIRE(spt->m_type == 0x1D);
		u->initiateIntentToDoSpecialPower(spt, building, nullptr, 0, 0);
		if (!u->isActive())
		{
			return false;
		}
		for (int i = 0; i < 300 && building->getTeam() != targetTeam; ++i)
		{
			frames(1);
		}
		return building->getTeam() == targetTeam;
	}
	static bool hasUpgrade(Object *o) { return o->hasUpgrade("P201Upgrade"); }
	static int queueOf(Object *o)
	{
		ProductionUpdateInterface *pu = o->getProductionUpdate();
		REQUIRE(pu != nullptr);
		return pu->getProductionCount();
	}
	bool queueOne(Object *o)
	{
		ProductionUpdateInterface *pu = o->getProductionUpdate();
		REQUIRE(pu != nullptr);
		const ThingTemplate *dummy = w.get("Dummy");
		REQUIRE(dummy != nullptr);
		return pu->queueCreateUnit(dummy, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false);
	}
};
} // namespace

TEST_CASE("PORT-201 capture: the upgrades of the captured building are reset (RW 0x854717 -> RW 0x68E083), a building nobody captures keeps "
		  "them")
{
	Port201World f;
	Player *alice = f.alice();
	Player *bob = f.bob();
	REQUIRE(alice);
	REQUIRE(bob);
	Team *aliceTeam = alice->getDefaultTeam();
	REQUIRE(aliceTeam);

	Object *taken = f.hall("P201Hall", *bob, 300.0f, 300.0f);
	Object *kept = f.hall("P201Hall", *bob, 700.0f, 700.0f);
	Object *capturer = f.hall("P201Capturer", *alice, 320.0f, 300.0f);
	taken->giveUpgrade("P201Upgrade"); // RW 0x693817's mask bit
	kept->giveUpgrade("P201Upgrade");
	REQUIRE(Port201World::hasUpgrade(taken));
	REQUIRE(Port201World::hasUpgrade(kept));

	REQUIRE(f.capture(capturer, taken, aliceTeam));
	CHECK_FALSE(Port201World::hasUpgrade(taken)); // RW 0x854717 cleared the mask before the team changed
	CHECK(Port201World::hasUpgrade(kept));        // the neighbouring building is untouched
}

TEST_CASE("PORT-201 capture: the production the previous owner queued is cancelled (RW 0x69973D -> production slot 0x40, RW 0x8A1666 -> "
		  "slot 0x3C, RW 0x8A05B2)")
{
	Port201World f;
	Player *alice = f.alice();
	Player *bob = f.bob();
	REQUIRE(alice);
	REQUIRE(bob);
	Team *aliceTeam = alice->getDefaultTeam();
	REQUIRE(aliceTeam);

	Object *taken = f.hall("P201Hall", *bob, 300.0f, 500.0f);
	Object *kept = f.hall("P201Hall", *bob, 700.0f, 300.0f);
	Object *capturer = f.hall("P201Capturer", *alice, 320.0f, 500.0f);
	REQUIRE(f.queueOne(taken));
	REQUIRE(f.queueOne(kept));
	REQUIRE(Port201World::queueOf(taken) == 1);
	REQUIRE(Port201World::queueOf(kept) == 1);

	REQUIRE(f.capture(capturer, taken, aliceTeam));
	CHECK(Port201World::queueOf(taken) == 0); // RW 0x8A05B2: the new owner does not inherit the enemy's queue (the owner's P-122)
	CHECK(Port201World::queueOf(kept) == 1);  // the neighbouring building is untouched
}

TEST_CASE("PORT-201 capture: a building that already belongs to the capturer's team loses neither its upgrades nor its queue (the early "
		  "returns of RW 0x6996F2 .. 0x699737)")
{
	Port201World f;
	Player *alice = f.alice();
	REQUIRE(alice);
	Team *aliceTeam = alice->getDefaultTeam();
	REQUIRE(aliceTeam);

	Object *own = f.hall("P201Hall", *alice, 500.0f, 500.0f);
	Object *capturer = f.hall("P201Capturer", *alice, 520.0f, 500.0f);
	own->giveUpgrade("P201Upgrade");
	REQUIRE(f.queueOne(own));
	REQUIRE(Port201World::hasUpgrade(own));
	REQUIRE(Port201World::queueOf(own) == 1);

	// the ability finds nothing to take: both the target and the building are already the capturer's team
	CHECK_FALSE(f.capture(capturer, own, aliceTeam));
	CHECK(Port201World::hasUpgrade(own));
	CHECK(Port201World::queueOf(own) == 1);
}

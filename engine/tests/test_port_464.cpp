// PORT-464 (issue #464, the parked Port 197): the per-frame steering of a summoned helper — slot 1 of the AISpecialPowerUpdate decision.
// Synthetic INI, no ROTWK_INSTALL / OPENBFME_ENNORATH_INSTALL. The expectations are the RotWK 2.01 binary's rules:
//   * RW 0x993019 is the decision base class's slot 1: ++counter (+ 0x10), then slot 0x14 (empty). RW 0x9EB961 (TARGETAOE_SUMMON), RW 0x9E8A42
//     (ASSIST_BATTLE_BUFF / _DEBUFF) and RW 0x9E7F0F (SHROUD_REVEAL) override it and chain to it (RW 0x9EBBDC's `call 0x993019`).
//   * the helper's name is the power's own (RW 0x9EB9A5: while the decision's string is empty, RW 0x401E64 true, the module RW 0x68C26D of the button's
//     template answers its slot 0x5C; an OCLSpecialPower's is RW 0x8C74C4 -> pickOCL RW 0x8C7393 -> the list's first created template).
//   * the helper is the player's HERO (RW 0x99316A: the AI record's army list, then its heroes; a STRUCTURE and, with the flag all three callers pass
//     (RW 0x9EBA57's `push 1`), anything but HERO is skipped) whose template name matches.
//   * RW 0x9EB961's body: the caster's current victim (RW 0x668303), the helper's own position for a melee weapon (5 units, RW 0xC69574) or its path goal
//     for a weapon with range / no weapon (30 units, RW 0xBE5AF8), and RW 0x66C4CA's move at the victim's position. GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AISpecialPowerUpdate.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <string>

namespace
{
const char kSkirmishAIData[] = "SkirmishAIData TheSkirmishAIData\nEnd\n";

// the shape of Ennorath 1.9.8.2's AngmarDurmarth (data/ini/object/evilfaction/units/angmar/angmardurmarth.ini: OCLSpecialPower SpecialAbilityTrolldesNordens,
// OCL OCL_SpawnSummonRogash, and AISpecialPowerUpdate RoyalGuardAI, SpecialPowerAIType = AI_SPECIAL_POWER_TARGETAOE_SUMMON): the summoner, the hero its
// list creates and a victim that does not fight back
const char kObjectsTemplate[] =
	"SpecialPower Port464Summon\n"
	"  Enum = SPECIAL_SPELL_BOOK_MEN_OF_DALE_ALLIES\n"
	"  ReloadTime = 1000\n"
	"End\n"
	"ObjectCreationList Port464OCL\n"
	"  CreateObject\n"
	"    ObjectNames = Port464Helper\n"
	"    Count = 1\n"
	"  End\n"
	"End\n"
	"CommandButton Command_Port464Summon\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = Port464Summon\n"
	"  TextLabel = CONTROLBAR:Port464Summon\n"
	"  DescriptLabel = CONTROLBAR:Port464SummonDescription\n"
	"End\n"
	"Object Port464Summoner\n"
	"  KindOf = INFANTRY SELECTABLE HERO\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 1\n"
	"  GeometryMinorRadius = 1\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = OCLSpecialPower ModuleTag_OCL\n"
	"    SpecialPowerTemplate = Port464Summon\n"
	"    OCL = Port464OCL\n"
	"    CreateLocation = CREATE_AT_LOCATION\n"
	"  End\n"
	"  Behavior = AISpecialPowerUpdate ModuleTag_AIPower\n"
	"    CommandButtonName = Command_Port464Summon\n"
	"    SpecialPowerAIType = AI_SPECIAL_POWER_TARGETAOE_SUMMON\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port464Helper\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 1\n"
	"  GeometryMinorRadius = 1\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 1\n"
	"  End\n"
	"End\n"
	"Object Port464Victim\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 1\n"
	"  GeometryMinorRadius = 1\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

// the summoner's own power module on the object
AISpecialPowerUpdate *powerOf(Object *obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (AISpecialPowerUpdate *p = dynamic_cast<AISpecialPowerUpdate *>(m.get()))
		{
			return p;
		}
	}
	return nullptr;
}

struct SummonFx : combattest::CombatWorld
{
	SkirmishAIStore aiStore{ w.keys };
	INIEnvironment aiEnv;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	CommandStore commands; // the module's init finds its button by name (RW 0x69156B), initAsIfInCommandSetForTest supplies it
	CommandStore *savedCommands = TheCommandStore;
	ObjectCreationListStore ocls{ w.keys };
	ObjectCreationListStore *savedOcl = TheObjectCreationListStore;
	Object *summoner = nullptr;
	Object *helper = nullptr;
	Object *victim = nullptr;

	// the helper's weapon is the only per-world difference: RW 0x9EB961 branches on it (melee: RW 0x9EBACA's own position, ranged: RW 0x9EBB4F's
	// path goal), and the helper must stay the template the OCL creates so the name lookup RW 0x99316A finds it
	explicit SummonFx(const char *helperWeapon = "SwordWeapon")
	{
		TheSpecialPowerStore = &powers;
		TheCommandStore = &commands;
		commands.setThingFactory(&w.things);
		TheObjectCreationListStore = &ocls;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
		SpecialPowerModules::registerAll(w.modules);
		std::string objects = kObjectsTemplate;
		const std::string from = "PRIMARY SwordWeapon";
		objects.replace(objects.find(from), from.size(), std::string("PRIMARY ") + helperWeapon + "\n");
		const std::string err = w.load(objects);
		REQUIRE_MESSAGE(err.empty(), err);
		aiStore.registerBlocks(aiEnv.blocks);
		INI ini(aiEnv);
		const std::string text = kSkirmishAIData;
		ini.loadMemory("skirmishaidata.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		logic->skirmishAI().setStore(&aiStore);
		REQUIRE(logic->skirmishAI().newGame(true) == 1);
		REQUIRE(logic->skirmishAI().findAI(bob()->getPlayerIndex())); // the module's update needs a skirmish AI (RW 0x6A950B)
		REQUIRE(!logic->skirmishAI().findAI(alice()->getPlayerIndex()));
	}
	~SummonFx()
	{
		TheSpecialPowerStore = savedPowers;
		TheCommandStore = savedCommands;
		TheObjectCreationListStore = savedOcl;
	}
	Player *bob() { return players.findPlayerWithName("Bob"); }
	Player *alice() { return players.findPlayerWithName("Alice"); }

	// the scene: Bob's summoner and his summoned helper, Alice's victim
	void scene(float helperX, float helperY, float victimX, float victimY)
	{
		summoner = spawn("Port464Summoner", 100.0f, 100.0f, 0.0f, teamOf("Bob"));
		helper = spawn("Port464Helper", helperX, helperY, 0.0f, teamOf("Bob"));
		victim = spawn("Port464Victim", victimX, victimY, 0.0f, teamOf("Alice"));
		REQUIRE(summoner);
		REQUIRE(helper);
		REQUIRE(victim);
	}

	// RW 0x8B714F runs on the module's first update and leaves it inactive when no command set holds the button (the synthetic world has no
	// command set), so the test initialises it first, as if the set held Command_Port464Summon; then the AI's own first update runs
	AISpecialPowerUpdate *armPower()
	{
		AISpecialPowerUpdate *p = powerOf(summoner);
		REQUIRE(p);
		p->initAsIfInCommandSetForTest();
		REQUIRE(p->active());
		REQUIRE(p->decision());
		frames(2);
		return p;
	}
};
} // namespace

TEST_CASE("PORT-464: a computer player's summoned helper walks to the caster's victim (RW 0x9EB961, 0x66C4CA)")
{
	SummonFx f;
	f.scene(100.0f, 140.0f, 300.0f, 300.0f);
	AISpecialPowerUpdate *power = f.armPower();

	f.summoner->getAIUpdateInterface()->setCurrentVictim(f.victim); // RW 0x668303 reads it
	f.frames(2);

	// the name comes from the power's own list (RW 0x9EB9A5 -> RW 0x8C74C4) and the helper was found and cached (RW 0x99316A, RW 0x9EBA86)
	CHECK(power->decision()->helperName == std::string("Port464Helper"));
	CHECK(power->decision()->helperId == f.helper->getID());

	// RW 0x9EB961: the helper's own position (it is 160 units away from the victim, far above the 5 of RW 0xC69574) and RW 0x66C4CA's move
	const Coord3D goal = f.helper->getAIUpdateInterface()->machineGoalPosition();
	CHECK(goal.x == doctest::Approx(300.0f).epsilon(0.001)); // tolerance: RW 0x66C4CA copies the victim's position, but the port's move clips the goal to the pathfinder's cell centre
	CHECK(goal.y == doctest::Approx(300.0f).epsilon(0.001)); // tolerance: as above
}

TEST_CASE("PORT-464: a helper already close enough to the victim is not re-ordered, and a caster without a victim steers nothing (RW 0x9EB961, 0xC69574, 0xBE5AF8, 0x668303)")
{
	// the melee branch (RW 0x9EBACA): the helper's own position against the victim, 5 units (RW 0xC69574). The helper stands 3 units away; an order
	// to the spot it already holds selects its weapon (before any order a synthetic helper has none), so RW 0x9EB961's weapon test takes the melee branch
	SummonFx f;
	f.scene(297.0f, 300.0f, 300.0f, 300.0f);
	f.armPower();
	Coord3D own{ 200.0f, 140.0f, 0.0f };
	f.helper->getAIUpdateInterface()->aiMoveToPosition(own, CMD_FROM_PLAYER);
	f.frames(1);
	f.summoner->getAIUpdateInterface()->setCurrentVictim(f.victim);
	f.frames(2);
	const Coord3D kept = f.helper->getAIUpdateInterface()->machineGoalPosition();
	CHECK(kept.x == 200.0f); // the helper's own order, not the victim's position (300)
	CHECK(kept.y == 140.0f);

	// the goal branch (RW 0x9EBB4F): a helper with a weapon of range (BowWeapon) is tested by its path goal, 30 units (RW 0xBE5AF8). Its order sends
	// it to a spot 10 units from the victim, so the frame issues no second order and the goal stays at that spot, not at the victim's position
	SummonFx g("BowWeapon");
	g.scene(100.0f, 140.0f, 300.0f, 300.0f);
	g.armPower();
	Coord3D far{ 290.0f, 300.0f, 0.0f };
	g.helper->getAIUpdateInterface()->aiMoveToPosition(far, CMD_FROM_PLAYER);
	g.frames(1);
	g.summoner->getAIUpdateInterface()->setCurrentVictim(g.victim);
	g.frames(1);
	const Coord3D near = g.helper->getAIUpdateInterface()->machineGoalPosition();
	CHECK(near.x == 290.0f); // the helper's own order, not the victim's position (300)
	CHECK(near.y == 300.0f);

	// no victim: RW 0x9EBADB's RW 0x668303 answers null and the frame steers nothing, so the helper never gets a goal
	SummonFx h;
	h.scene(100.0f, 140.0f, 300.0f, 300.0f);
	h.armPower();
	h.frames(2);
	const Coord3D idle = h.helper->getAIUpdateInterface()->machineGoalPosition();
	CHECK(idle.x == 0.0f);
	CHECK(idle.y == 0.0f);
}


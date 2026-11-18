// OpenBFME. GPL-3.0. Synthetic INI only; no retail or mod mounts.
// A soldier's drawable chooses its horde before the control bar supplies a button (RW 0x7108F2,
// DONOR BFME2 0x70F14C, GameClient/Rva0030F14CSelect.cpp). Neither selection nor the two buttons
// make different power templates aliases (RW 0x68C26D, SpecialPowerModule slot 0 at RW 0x8969D1).

#include "doctest.h"

#include "SyntheticHudTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameClient/ControlBar.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StealthAbilityModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"

namespace
{
constexpr char kIni[] = R"ini(
Upgrade Upgrade_P287ChargeReady
  Type = OBJECT
End
ModifierList P287ChargeBonus
  Category = SPELL
  Modifier = DAMAGE_MULT 175%
  Duration = 18000
End
SpecialPower P287OldCharge
  Enum = SPECIAL_ATTRIBUTEMOD_CANCELDISGUISE
  ReloadTime = 5000
End
SpecialPower P287Charge
  Enum = SPECIAL_ATTRIBUTEMOD_CANCELDISGUISE
  ReloadTime = 7000
End
SpecialPower P287Cloak
  Enum = SPECIAL_GENERAL_TARGETLESS_TWO
  ReloadTime = 8000
End
CommandButton P287OldButton
  Command = SPECIAL_POWER
  SpecialPower = P287OldCharge
  Options = OK_FOR_MULTI_SELECT OK_FOR_MULTI_EXECUTE NEED_TARGET_ENEMY_OBJECT
  InPalantir = Yes
  AutoAbility = Yes
End
CommandButton P287ChargeButton
  Command = SPECIAL_POWER
  SpecialPower = P287Charge
  Options = OK_FOR_MULTI_SELECT OK_FOR_MULTI_EXECUTE
  InPalantir = Yes
End
CommandButton P287CloakButton
  Command = SPECIAL_POWER
  SpecialPower = P287Cloak
  Options = OK_FOR_MULTI_SELECT OK_FOR_MULTI_EXECUTE
  InPalantir = Yes
  AutoAbility = Yes
  TriggerWhenReady = Yes
End
CommandSet P287SoldierSet
  1 = P287OldButton
End
CommandSet P287ChargeSet
  1 = P287ChargeButton
End
CommandSet P287CloakSet
  1 = P287CloakButton
End
Weapon P287TestHit
  AttackRange = 150
  DamageNugget
    Damage = 16
    DamageType = SLASH
  End
End
Object P287Dummy
  KindOf = SELECTABLE INFANTRY
  Body = ActiveBody BodyTag
    MaxHealth = 400
  End
End
Object P287Soldier
  KindOf = SELECTABLE INFANTRY HORDE_MEMBER
  CommandSet = P287SoldierSet
  Body = ActiveBody BodyTag
    MaxHealth = 400
  End
  Behavior = AttributeModifierPoolUpdate ModifierPoolTag
  End
  Behavior = SpecialPowerModule SoldierStarter
    SpecialPowerTemplate = P287Charge
    AvailableAtStart = Yes
    UpdateModuleStartsAttack = No
    AttributeModifier = P287ChargeBonus
    AttributeModifierAffectsSelf = Yes
  End
End
Object P287Horde
  KindOf = SELECTABLE INFANTRY HORDE
  Body = ActiveBody BodyTag
    MaxHealth = 400
  End
  Behavior = HordeContain ContainTag
    InitialPayload = P287Soldier 2
    RankInfo = RankNumber:1 UnitType:P287Soldier Position:X:20 Y:20 Position:X:20 Y:-20
  End
End
ChildObject P287ChargeHorde P287Horde
  CommandSet = P287ChargeSet
  Behavior = SpecialPowerModule ChargeStarter
    SpecialPowerTemplate = P287Charge
    StartsPaused = Yes
    AvailableAtStart = Yes
    UpdateModuleStartsAttack = No
    AttributeModifier = P287ChargeBonus
    AttributeModifierAffectsSelf = Yes
  End
  Behavior = UnpauseSpecialPowerUpgrade ChargeUnlock
    SpecialPowerTemplate = P287Charge
    TriggeredBy = Upgrade_P287ChargeReady
  End
End
ChildObject P287NoCloakPath P287Horde
  CommandSet = P287CloakSet
  Behavior = HordeAIUpdate AITag
  End
End
ChildObject P287CloakHorde P287NoCloakPath
  Behavior = SpecialPowerModule CloakStarter
    SpecialPowerTemplate = P287Cloak
    AvailableAtStart = Yes
    UpdateModuleStartsAttack = Yes
  End
  Behavior = ToggleHiddenSpecialAbilityUpdate CloakUpdate
    SpecialPowerTemplate = P287Cloak
    EffectDuration = 15000
  End
  Behavior = InvisibilityUpdate CloakInvisibility
    StartsActive = No
    InvisibilityNugget
      InvisibilityType = STEALTH
    End
  End
End
ChildObject P287MissingCloakStarter P287CloakHorde
  RemoveModule CloakStarter
End
)ini";

struct Rig : synthetichud::Rig
{
	WeaponStores weapons;
	SpecialPowerStore powers;
	CommandStore commandData;
	AttributeModifierStore modifiers;
	SpecialPowerStore *oldPowers = TheSpecialPowerStore;
	CommandStore *oldCommands = TheCommandStore;
	AttributeModifierStore *oldModifiers = TheAttributeModifierStore;
	AIWorld ai{ *logic, AIWorldConfig{}, w.fx.env.macros };
	SpellCommands spells;
	ControlBar bar{ input->context() };

	Rig()
	{
		LogicModules::registerAll(w.modules);
		weapons.install();
		TheSpecialPowerStore = &powers;
		TheCommandStore = &commandData;
		TheAttributeModifierStore = &modifiers;
		commandData.setThingFactory(&w.things);
		input->context().commands = &commandData;
		auto &blocks = w.fx.env.blocks;
		blocks.registerBlock("SpecialPower", &SpecialPowerStore::parseSpecialPowerDefinitionGlobal);
		blocks.registerBlock("CommandSet", &CommandStore::parseCommandSetDefinitionGlobal);
		blocks.registerBlock("CommandButton", &CommandStore::parseCommandButtonDefinitionGlobal);
		blocks.registerBlock("Weapon", &WeaponStore::parseWeaponTemplateDefinitionGlobal);
		modifiers.registerBlock(blocks);
		ai.attach(); // the cloak update needs the same live AI interface as the retail horde
		spells.registerHandlers(dispatch);
		logic->combat().setAutoAcquireEnabled(false);
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.5f;
		logic->settings().unitReallyDamagedThreshold = 0.1f;
		logic->settings().invisibilityRulesLoaded = true;
	}

	~Rig()
	{
		logic->reset();
		TheAttributeModifierStore = oldModifiers;
		TheCommandStore = oldCommands;
		TheSpecialPowerStore = oldPowers;
	}

	const ControlBarButton *button(const char *name)
	{
		bar.update();
		for (const auto &b : bar.palantirButtons())
		{
			if (b.button && b.button->m_name == name)
			{
				return &b;
			}
		}
		return nullptr;
	}

	bool press(const ControlBarButton &b)
	{
		const bool sent = bar.pressButton(b.slot, b.inPalantir);
		input->update();
		return sent;
	}

	void load()
	{
		const auto error = w.load(kIni, INI_LOAD_OVERWRITE, "port287.ini");
		REQUIRE_MESSAGE(error.empty(), error);
	}

	void frames(unsigned n)
	{
		for (unsigned i = 0; i < n; ++i)
		{
			logic->runLogicFrame();
		}
	}

	const SpecialPowerTemplate *power(const char *name)
	{
		const auto *t = powers.findSpecialPowerTemplate(name);
		REQUIRE(t);
		return t;
	}

	float hit(Object &source, Object &target)
	{
		const auto *w = weapons.weapons().findWeaponTemplate("P287TestHit");
		REQUIRE(w);
		const float before = target.getBodyModule()->getHealth();
		DeliverNuggets(*logic, source.getID(), *w, WeaponBonus{}, &target, target.getPosition(), false, nullptr);
		return before - target.getBodyModule()->getHealth();
	}
};

void moveTo(Object &o, float x, float y)
{
	const Coord3D at{ x, y, 0.0f };
	o.setPosition(&at);
}

HordeContain &hordeContain(Object &o)
{
	auto *contain = dynamic_cast<HordeContain *>(o.getContain());
	REQUIRE(contain);
	REQUIRE(contain->getContainCount() == 2);
	return *contain;
}
} // namespace

TEST_CASE("PORT-287 charge: clicking or boxing a released soldier casts the horde's damage buff")
{
	Rig r;
	r.load();
	Object *horde = r.place("P287ChargeHorde", 100, 100);
	Object *victim = r.place("P287Dummy", 180, 100, "Bob");
	HordeContain &contain = hordeContain(*horde);
	Object *released = contain.getContainedItemsList()->front();
	Object *fighting = contain.getContainedItemsList()->back();
	// The release (RW 0x86EBF4) takes the soldier out of the contain list but leaves its ProducerID.
	contain.releaseMemberForGarrison(released);
	moveTo(*released, 250, 100);
	REQUIRE(released->getContainedBy() == nullptr);
	REQUIRE(released->getProducerID() == horde->getID());
	CHECK(r.hit(*fighting, *victim) == 16.0f);
	CHECK(r.hit(*released, *victim) == 16.0f);

	SUBCASE("click the soldier") { r.click(250, 100); }
	SUBCASE("box only the soldier") { r.drag(230, 80, 270, 120); }
	CHECK(HudObjects::pickObject(r.input->context(), { 250, 100 }) == released); // ordinary order pick still sees the raw soldier
	r.selected({ horde->getID() });
	REQUIRE(r.button("P287OldButton") == nullptr);
	const ControlBarButton *button = r.button("P287ChargeButton");
	REQUIRE(button);
	CHECK(button->state != ButtonState::Enabled);
	horde->giveUpgrade("Upgrade_P287ChargeReady"); // real UnpauseSpecialPowerUpgrade
	r.frames(1);
	button = r.button("P287ChargeButton");
	REQUIRE(button);
	CHECK(button->state == ButtonState::Enabled);
	REQUIRE(r.press(*button));
	const GameMessage &cast = r.onlyCommand(MSG_DO_SPECIAL_POWER);
	REQUIRE(cast.getArgumentCount() == 4);
	CHECK(cast.getArgument(0)->integer == (int)r.power("P287Charge")->getID());
	CHECK(cast.getArgument(3)->objectID == INVALID_ID); // the horde is taken from the player's selection
	r.drain();

	// RW 0x897987 adds only the current member(s) to a horde's self-affecting modifier.
	// The ordinary damage path reads DAMAGE_MULT from that source (RW 0x90E4D2–0x90E4F3).
	CHECK(r.hit(*fighting, *victim) == 28.0f);
	CHECK(r.hit(*released, *victim) == 16.0f);
}

TEST_CASE("PORT-287 charge: without a horde producer the old targeted button does not alias a power with the same Enum")
{
	Rig r;
	r.load();
	Object *soldier = r.place("P287Soldier", 140, 100);
	Object *victim = r.place("P287Dummy", 200, 100, "Bob");
	SUBCASE("no producer") { CHECK(soldier->getProducerID() == INVALID_ID); }
	SUBCASE("a non-horde producer") { soldier->setProducer(r.place("TestUnarmed", 80, 80)); }
	r.drag(120, 80, 160, 120);
	r.selected({ soldier->getID() });
	r.click(140, 100);
	r.selected({ soldier->getID() });
	const auto *actual = r.power("P287Charge");
	const auto *old = r.power("P287OldCharge");
	REQUIRE(actual->getID() != old->getID());
	REQUIRE(actual->getSpecialPowerType() == old->getSpecialPowerType());
	REQUIRE(SpecialPowerModules::findModule(*soldier, actual));
	CHECK(SpecialPowerModules::findModule(*soldier, old) == nullptr);
	const ControlBarButton *button = r.button("P287OldButton");
	REQUIRE(button);
	CHECK(button->state == ButtonState::Enabled);
	REQUIRE(r.press(*button));
	CHECK(r.input->ui().getGUICommand() == button->button);
	r.click(200, 100);
	CHECK(std::none_of(r.commands.messages().begin(), r.commands.messages().end(), [](const GameMessage &m) {
		return m.getType() == MSG_DO_SPECIAL_POWER_AT_OBJECT;
	}));
	r.drain();
	r.frames(5);
	CHECK(r.hit(*soldier, *victim) == 16.0f);
}

TEST_CASE("PORT-287 selection: an unselectable soldier or horde producer is not selected")
{
	Rig r;
	r.load();
	Object *horde = r.place("P287ChargeHorde", 100, 100);
	HordeContain &contain = hordeContain(*horde);
	Object *soldier = contain.getContainedItemsList()->front();
	contain.releaseMemberForGarrison(soldier);
	moveTo(*soldier, 240, 100);
	const auto status = (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
	SUBCASE("horde producer") { horde->setStatus(status, true); }
	SUBCASE("raw soldier") { soldier->setStatus(status, true); }
	r.click(240, 100);
	r.selected({});
	r.drag(220, 80, 260, 120);
	r.selected({});
}

TEST_CASE("PORT-287 cloak: the starter hides real members; the same button without a starter does not")
{
	Rig r;
	r.load();
	const char *type = "P287NoCloakPath";
	bool hasStarter = false;
	SUBCASE("a horde carrying the cloak starter and update") { type = "P287CloakHorde"; hasStarter = true; }
	SUBCASE("same visible button and update, no starter") { type = "P287MissingCloakStarter"; }
	SUBCASE("button only, the archive's Warrior shape") { }
	Object *horde = r.place(type, 100, 100);
	REQUIRE(horde->getAIUpdateInterface());
	CHECK((SpecialPowerModules::findModule(*horde, r.power("P287Cloak")) != nullptr) == hasStarter);
	HordeContain &contain = hordeContain(*horde);
	std::vector<ObjectID> members;
	for (const Object *m : *contain.getContainedItemsList())
	{
		members.push_back(m->getID());
	}
	const Coord3D at = *contain.getContainedItemsList()->front()->getPosition();
	const Player *enemy = r.players.findPlayerWithName("Bob");
	REQUIRE(enemy);
	for (ObjectID id : members)
	{
		CHECK_FALSE(InvisibilityManager::isStealthedAndUndetected(*r.logic->findObjectByID(id), enemy));
	}
	r.click((int)at.x, (int)at.y);
	r.selected({ horde->getID() });
	const ControlBarButton *button = r.button("P287CloakButton");
	REQUIRE(button);
	CHECK(button->state == ButtonState::Enabled);
	REQUIRE(r.press(*button));
	const GameMessage &cast = r.onlyCommand(MSG_DO_SPECIAL_POWER);
	REQUIRE(cast.getArgumentCount() == 4);
	CHECK(cast.getArgument(0)->integer == (int)r.power("P287Cloak")->getID());
	r.drain();
	r.frames(20);
	const auto *update = ToggleHiddenSpecialAbilityUpdate::of(*horde);
	CHECK((update ? update->hides() : 0) == (hasStarter ? 1u : 0u));

	// RW 0x8980A3 requires a starter for RW 0x897E87 to initiate the update. RW 0x8B1909
	// and RW 0x8B1A70 start InvisibilityUpdate on that horde. The live members must be hidden
	// from the enemy, not just a cooldown or a HIDDEN model condition on the horde itself.
	for (ObjectID id : members)
	{
		const auto *m = r.logic->findObjectByID(id);
		REQUIRE(m);
		CHECK(InvisibilityManager::isStealthedAndUndetected(*m, enemy) == hasStarter);
	}
}

// PORT-277: first check the existing main path before changing the engine.
// Synthetic INI only. RW 0x911A58: a horde locks LockWeaponSlot temporarily,
// then orders members through HordeContainInterface vtable +0x04 (RW 0x875221).
// A single firer without a contain returns without an attack (RW 0x911A58, contain check at +0x1E).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

using namespace combattest;
namespace
{
const char kHit[] = R"ini(
Weapon P277Hit
AttackRange = 400
DelayBetweenShots = 10000
DamageNugget
	Damage = 7
	DamageType = MAGIC
	DeathType = NORMAL
End
End
)ini";
const char kPort277[] =
	"Weapon P277Rangefinder\n"
	"  AttackRange = 150\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  HordeAttackNugget\n"
	"    LockWeaponSlot = SECONDARY\n"
	"  End\n"
	"End\n"
	"Object P277Archer\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 250\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY BowWeapon\n"
	"    Weapon = SECONDARY P277Hit\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object P277ArcherHorde\n"
	"  KindOf = HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 250\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY P277Rangefinder\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:P277Archer Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:P277Archer Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = P277Archer 6\n"
	"    RanksToReleaseWhenAttacking = 1 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("PORT-277: existing rangefinder orders members to fire SECONDARY and damage the target")
{
	std::string ini = std::string(kHit) + kPort277;
	SUBCASE("member weapon has a direct DamageNugget") {}
	SUBCASE("member weapon has only a ProjectileNugget; damage is in its warhead")
	{
		const std::string assignment = "Weapon = SECONDARY P277Hit";
		const auto at = ini.find(assignment);
		REQUIRE(at != std::string::npos);
		ini.replace(at, assignment.size(), "Weapon = SECONDARY Arc20");
	}
	CombatWorld w(ini.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P277ArcherHorde", 'A', 300, 300);
	Object *target = w.unit("Dummy", 'B', 430, 300);
	w.frames(12);
	const auto members = w.membersOf(horde);
	REQUIRE(members.size() == 6);
	const auto *rangefinder = w.stores.weapons().findWeaponTemplate("P277Rangefinder");
	REQUIRE(rangefinder != nullptr);
	const float health = w.health(target);
	// No ordinary attack order to the horde: only the ability's extra weapon fires.
	ObjectWeapons::createAndFireTempWeaponAt(rangefinder, horde, *target);
	CHECK(w.hordeOf(horde)->attackStats().orders == 6);
	CHECK(w.health(target) == health); // the rangefinder has no damage of its own
	for (Object *m : members)
	{
		REQUIRE(m->getWeapons() != nullptr);
		CHECK(m->getWeapons()->curSlot() == 1);
		CHECK(m->getWeapons()->isCurWeaponLocked());
	}
	// tolerance: timeout only; direct hits and projectile flight have different arrival frames.
	CHECK(w.runUntil([&] { return w.health(target) < health; }, 100) < 100);
	// tolerance: stop at the first damaging frame; one or more of six shots can arrive together.
	CHECK(w.health(target) <= health - 7.0f);
}

TEST_CASE("PORT-277: single firer with no HordeContain does not turn the rangefinder into a damaging weapon")
{
	const std::string ini = std::string(kHit) + kPort277;
	CombatWorld w(ini.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *single = w.unit("P277Archer", 'A', 300, 300);
	Object *target = w.unit("Dummy", 'B', 430, 300);
	w.frames(12);
	REQUIRE(single->getContain() == nullptr);
	const auto *rangefinder = w.stores.weapons().findWeaponTemplate("P277Rangefinder");
	REQUIRE(rangefinder != nullptr);
	const float health = w.health(target);
	ObjectWeapons::createAndFireTempWeaponAt(rangefinder, single, *target);
	w.frames(20);
	CHECK(w.health(target) == health);
	CHECK(single->getWeapons()->curSlot() == 0);
	CHECK_FALSE(single->getWeapons()->isCurWeaponLocked());
}

// RW 0x911A58 / RW 0x911AC9 only lock and dispatch; RW 0x875221 /
// RW 0x875550 themselves only dispatch. DRAFT S-7474: these functions
// do not establish the subsequent AI weapon choice. The expectation below
// failed on main and must be checked against that downstream RW path; it
// is deliberately not weakened or reported as a passing protection.
TEST_CASE("PORT-277: a dummy member weapon never borrows damage from PRIMARY or the rangefinder")
{
	std::string ini = std::string(kHit) + kPort277;
	const std::string assignment = "Weapon = SECONDARY P277Hit";
	const auto at = ini.find(assignment);
	REQUIRE(at != std::string::npos);
	ini.replace(at, assignment.size(), "Weapon = SECONDARY P277Dummy");
	ini += "Weapon P277Dummy\n AttackRange = 400\n NoVictimNeeded = Yes\n End\n";
	CombatWorld w(ini.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *source = w.unit("P277ArcherHorde", 'A', 300, 300);
	Object *target = w.unit("Dummy", 'B', 430, 300);
	w.frames(12);
	const float health = w.health(target);
	const auto *weapon = w.stores.weapons().findWeaponTemplate("P277Rangefinder");
	REQUIRE(weapon != nullptr);
	SUBCASE("object target")
	{
		ObjectWeapons::createAndFireTempWeaponAt(weapon, source, *target);
		CHECK(w.hordeOf(source)->attackStats().orders == 6);
	}
	SUBCASE("position target")
	{
		ObjectWeapons::createAndFireTempWeapon(weapon, source, *target->getPosition());
		CHECK(w.hordeOf(source)->attackStats().positionOrders == 6);
	}
	for (Object *member : w.membersOf(source)) CHECK(member->getWeapons()->curSlot() == 1);
	w.frames(100);
	CHECK(w.health(target) == health);
}

// The proprietary definitions stay outside Git. Point OPENBFME_ENNORATH_INI_DIR
// at the unmodified, extracted Ennorath 1.9.8.3 merged INI archive (data/ini
// below that directory). With full game installs, the normal scenario mount is
// also supported. No custom INI parser or copied retail block is used here.
#include "EnnorathScenario.h"
#include "SweepEffectProbe.h"
#include "BigTestUtil.h"
#include "GameLogic/ActionManager.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/WeaponNugget.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace port277real
{
struct Corpus
{
	retailtest::Mount mount;
	hudtest::SharedWorld shared;

	explicit Corpus(const std::filesystem::path &root)
	{
		REQUIRE(std::filesystem::is_regular_file(root / "data/ini/weapon.ini"));
		initest::FileList files;
		for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
		{
			if (!entry.is_regular_file()) continue;
			std::ifstream in(entry.path(), std::ios::binary);
			REQUIRE_MESSAGE(in.good(), entry.path().u8string());
			std::string bytes{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
			REQUIRE_FALSE(in.bad());
			files.emplace_back(std::filesystem::relative(entry.path(), root).generic_u8string(), std::move(bytes));
		}
		mount.fs = std::make_unique<Win32BIGFileSystem>();
		std::string error;
		auto archive = mount.fs->openArchiveFromMemory("port277-external.big",
			std::make_shared<const std::vector<std::uint8_t>>(bigtest::makeBig("BIGF", files)), &error);
		REQUIRE_MESSAGE(archive != nullptr, error);
		mount.fs->mountArchive(std::move(archive), "port277-external.big", false);
		shared.mount = &mount;
		shared.world = std::make_unique<RetailObjectWorld>(*mount.fs);
		REQUIRE_MESSAGE(shared.world->load(&error), error);
		for (const auto &e : shared.world->report().errors)
			MESSAGE(e.file << ": " << e.message);
		REQUIRE(shared.world->report().errors.empty());
	}
};

hudtest::SharedWorld *world()
{
	if (const char *dir = std::getenv("OPENBFME_ENNORATH_INI_DIR"); dir && *dir)
	{
		static Corpus corpus(dir);
		return &corpus.shared;
	}
	static ennorathscenario::World installed;
	if (!installed.load("PORT-277 real definitions: or set OPENBFME_ENNORATH_INI_DIR")) return nullptr;
	return &installed.shared;
}

struct Row
{
	const char *object;
	const char *button;
	const char *rangefinder;
	const char *memberWeapon;
	const char *faction;
	int slot;
	bool position;
	const char *ocl;
};

// Names, not copies of the mod's definitions. The expected graph is checked
// against the externally loaded data. RW 0x911A58 / RW 0x911AC9 dispatch;
// RW 0x875221 / RW 0x875550 only issue AI orders. Subsequent dummy-slot
// choice remains unverified (S-7474); these real tests are blocked by S-7473.
const Row rows[] = {
	{ "IsenfurtWchter", "Command_IsenfurtWchterSmite", "IsenfurtWchterSmiteRangeFinder", "", "FactionRohan", TERTIARY_WEAPON, false, "" },
	{ "IsenfurtWchterCreep", "Command_IsenfurtWchterSmite", "IsenfurtWchterSmiteRangeFinder", "", "FactionRohan", TERTIARY_WEAPON, false, "" },
	{ "IsenfurtWchterHorde", "Command_IsenfurtWchterSmite", "IsenfurtWchterSmiteRangeFinder", "IsenfurtWchterSpearThrown", "FactionRohan", TERTIARY_WEAPON, false, "" },
	{ "IsenfurtWchterHordeHerold", "Command_IsenfurtWchterSmite", "IsenfurtWchterSmiteRangeFinder", "IsenfurtWchterSpearThrown", "FactionRohan", TERTIARY_WEAPON, false, "" },
	{ "DwarvenAxtschwingerHorde", "Command_AxtschwingerAxeThrow", "DwarvenZealotBoomAxeRangeFinder", "DwarvenZealotBoomAxeThrownEdain", "FactionDwarves", TERTIARY_WEAPON, false, "" },
	{ "AngmarDarkRangerHorde", "Command_AngmarRangersIceArrowAbility", "HaradrimBarbedArrowModRangeFinder", "AngRangerIceShotArrowMod", "FactionAngmar", QUINARY_WEAPON, false, "" },
	{ "GondorRangerHorde", "Command_MenLongShotAbility", "MenLongShotWeapon", "MenLongShotFakeWeapon", "FactionMen", QUINARY_WEAPON, true, "OCL_LongShotAbilityGondor" },
	{ "DwarvenDain_mod", "Command_SpecialAbilityDrarSpeerfeuer", "MenLongShotWeapon", "", "FactionDwarves", QUINARY_WEAPON, true, "OCL_SpeerfeuerAbility" },
};

Object *place(structtest::Arena &arena, hudtest::SharedWorld &data, const char *name, int side, float x, float y)
{
	const auto *t = data.world->things().findTemplate(name);
	REQUIRE_MESSAGE(t != nullptr, name);
	Object *o = arena.logic.newObject(t, arena.player(side)->getDefaultTeam(), {});
	REQUIRE(o != nullptr);
	const Coord3D at{ x, y, 0.0f };
	o->setPosition(&at);
	o->setOrientation(0.0f);
	return testutil::completeHorde(o);
}

void frames(structtest::Arena &arena, unsigned count)
{
	for (unsigned i = 0; i < count; ++i) arena.logic.runLogicFrame();
}

// Observations at the ownership boundary, not assertions pinning a broken
// updater as expected behaviour. The after-shot assertions below remain RW's.
void diagnosePreShot(size_t index, hudtest::SharedWorld &data)
{
	if (index != 2 && index != 4 && index != 5) return;
	const Row &row = rows[index];
	structtest::Arena arena(data, row.faction, "FactionMordor", 160);
	arena.logic.combat().setAutoAcquireEnabled(false);
	Object *source = place(arena, data, row.object, 0, 500, 500);
	// angmarhordes.ini: the starter is unpaused by this level-one upgrade.
	if (index == 5) source->giveUpgrade("Upgrade_ObjectLevel1");
	Object *target = place(arena, data, "MordorFighter", 1, 650, 500);
	frames(arena, 30);
	const auto *button = data.world->commands().findCommandButton(row.button);
	REQUIRE(button != nullptr);
	const auto *power = data.world->spellStores().specialPowers().findSpecialPowerTemplate(button->m_specialPowerName);
	REQUIRE(power != nullptr);
	WeaponFireSpecialAbilityUpdate *updater = nullptr;
	for (const auto &m : source->modules())
	{
		auto *wf = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(m.get());
		if (wf && wf->drivesSpecialPower(power)) updater = wf;
	}
	REQUIRE(updater != nullptr);
	const auto *starter = dynamic_cast<const SpecialPowerModule *>(SpecialPowerModules::findModule(*source, power));
	REQUIRE(starter != nullptr);
	const int pausedBefore = starter->pauseCount();
	bool hurt = false;
	const ObjectID id = target->getID();
	arena.logic.combat().setDamageTrace([&](const Object &victim, std::uint32_t, float, float taken) {
		if (victim.getID() == id && taken > 0.0f) hurt = true;
	});
	const bool accepted = SpecialPowerModules::doSpecialPowerAtObject(*source, power, target, button->m_options, true);
	frames(arena, 300);
	auto *horde = dynamic_cast<HordeContain *>(source->getContain());
	MESSAGE("PORT-277 pre-shot " << row.object << ": dispatchAccepted=" << accepted
		<< " pausedBefore=" << pausedBefore << " initiates=" << starter->initiates()
		<< " shots=" << updater->shotsFired() << " packing=" << updater->packingState()
		<< " triggered=" << updater->triggerCount() << " damage=" << hurt
		<< " hordeFires=" << (horde ? horde->attackStats().fires : 0));
	arena.logic.combat().setDamageTrace({});
}

void checkRow(size_t index)
{
	auto *data = world();
	if (!data) return; // explicit SKIP printed above; synthetic cases always run
	const Row &row = rows[index];
	diagnosePreShot(index, *data);
	INFO(row.object << " / " << row.button);
	structtest::Arena arena(*data, row.faction, "FactionMordor", 160);
	arena.logic.combat().setAutoAcquireEnabled(false);
	Object *source = place(arena, *data, row.object, 0, 500, 500);
	Object *target = place(arena, *data, "MordorFighter", 1, 650, 500);
	const ObjectID targetID = target->getID();
	frames(arena, 30);
	REQUIRE(arena.logic.findObjectByID(targetID) != nullptr);
	const auto *button = data->world->commands().findCommandButton(row.button);
	REQUIRE(button != nullptr);
	const auto *power = data->world->spellStores().specialPowers().findSpecialPowerTemplate(button->m_specialPowerName);
	REQUIRE(power != nullptr);
	const auto *rangefinder = data->world->weaponStores().weapons().findWeaponTemplate(row.rangefinder);
	REQUIRE(rangefinder != nullptr);
	unsigned matchingUpdaters = 0;
	for (const auto &m : source->modules())
	{
		const auto *wf = dynamic_cast<const WeaponFireSpecialAbilityUpdate *>(m.get());
		if (!wf || !wf->ownedWeapon() || wf->ownedWeapon()->getTemplate() != rangefinder) continue;
		++matchingUpdaters;
		if (index != 3) CHECK(wf->drivesSpecialPower(power));
	}
	REQUIRE(matchingUpdaters == 1);
	REQUIRE(rangefinder->m_nuggets.size() == 1);
	const auto *nugget = dynamic_cast<const HordeAttackNugget *>(rangefinder->m_nuggets.front().get());
	REQUIRE(nugget != nullptr);
	CHECK(nugget->m_lockWeaponSlot == row.slot);

	auto *horde = dynamic_cast<HordeContain *>(source->getContain());
	std::set<ObjectID> memberIDs;
	if (*row.memberWeapon)
	{
		REQUIRE(horde != nullptr);
		for (Object *member : *horde->getContainedItemsList())
		{
			// Banners are not shooters in these formations.
			if (member->isKindOfName("BANNER")) continue;
			REQUIRE(member->getWeapons() != nullptr);
			const Weapon *weapon = member->getWeapons()->weaponInSlot(row.slot);
			REQUIRE(weapon != nullptr);
			CHECK(weapon->getTemplate()->getName() == row.memberWeapon);
			if (index == 6) CHECK(weapon->getTemplate()->m_nuggets.empty());
			memberIDs.insert(member->getID());
		}
		REQUIRE_FALSE(memberIDs.empty());
	}
	else
	{
		CHECK(horde == nullptr); // RW 0x911A58 / RW 0x911AC9: no contain, no effect
	}

	if (index == 3)
	{
		// RW 0x73B22F resolves the module's template name; RW 0x68C26D
		// only returns a matching interface. The button cannot reach this nugget.
		CHECK(data->world->spellStores().specialPowers().findSpecialPowerTemplate("SpecialAbilityWestfoldWchterSpear") == nullptr);
		CHECK(SpecialPowerModules::findModule(*source, power) == nullptr);
		CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*source, target, CMD_FROM_PLAYER, power, 0, true));
		CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*source, target, CMD_FROM_PLAYER, power, 0, false));
	}

	// Diagnose the matrix without changing or pinning its shared probe rules.
	// It inspects both the horde and members, including member modules that
	// could never drive a HordeAttackNugget (RW 0x911A58).
	const auto probe = sweep_probe::EffectProbe::take(arena.logic, source, power, 0, target);
	unsigned emptyDamageExpectations = 0;
	for (const auto &e : probe.expects)
		if (e.kind == sweep_probe::EffectProbe::DAMAGE && !e.weapon && e.weapons.empty())
			++emptyDamageExpectations;
	MESSAGE("PORT-277 matrix " << row.object << ": unfulfillable DAMAGE expectations=" << emptyDamageExpectations);

	bool memberDamage = false, anyDamage = false;
	arena.logic.combat().setDamageTrace([&](const Object &victim, std::uint32_t owner, float, float taken) {
		if (victim.getID() == targetID && taken > 0.0f)
		{
			anyDamage = true;
			memberDamage = memberDamage || memberIDs.count(owner) != 0;
		}
	});
	const Coord3D at = *target->getPosition();
	// Isolate our boundary: the extra weapon has ALREADY fired. The updater's
	// own functions are Local 76, not a second route added to the engine.
	if (row.position) ObjectWeapons::createAndFireTempWeapon(rangefinder, source, at);
	else ObjectWeapons::createAndFireTempWeaponAt(rangefinder, source, *target);
	if (horde)
	{
		if (row.position) CHECK(horde->attackStats().positionOrders == memberIDs.size());
		else CHECK(horde->attackStats().orders == memberIDs.size());
		for (ObjectID id : memberIDs)
		{
			Object *member = arena.logic.findObjectByID(id);
			REQUIRE(member != nullptr);
			CHECK(member->getWeapons()->curSlot() == row.slot);
		}
	}
	frames(arena, 180);
	if (index >= 2 && index <= 5) CHECK(memberDamage);
	else CHECK_FALSE(anyDamage); // index 6: draft expectation, still needs RW selection proof (S-7474)
	arena.logic.combat().setDamageTrace({});

	if (*row.ocl)
	{
		// Long Shot's real damage is a separate OCLSpecialPower, not a
		// fallback to PRIMARY or a hidden DamageNugget in the rangefinder.
		auto *module = dynamic_cast<OCLSpecialPower *>(SpecialPowerModules::findModule(*source, power));
		REQUIRE(module != nullptr);
		const auto *list = TheObjectCreationListStore->findObjectCreationList(row.ocl);
		REQUIRE(list != nullptr);
		bool oclDamage = false;
		std::set<std::string> damageSources;
		arena.logic.combat().setDamageTrace([&](const Object &victim, std::uint32_t owner, float, float taken) {
			if (victim.getControllingPlayer() != arena.player(1) || taken <= 0.0f) return;
			const Object *attacker = arena.logic.findObjectByID(owner);
			if (!attacker || !attacker->getTemplate()) return;
			const std::string name = attacker->getTemplate()->getName();
			damageSources.insert(name);
			oclDamage = oclDamage || name == (index == 6 ? "MenLongShotObjectGondor" : "DrarSpeerfeuerProjectile");
		});
		// Cover Speerfeuer's spread with actual enemies, not a changed OCL.
		for (int x = -4; x <= 4; ++x)
			for (int y = -4; y <= 4; ++y)
				place(arena, *data, "MordorFighter", 1, at.x + x * 50.0f, at.y + y * 50.0f);
		module->doSpecialPowerAtLocation(at, 0);
		frames(arena, 600);
		for (const auto &name : damageSources) MESSAGE("PORT-277 OCL damage source: " << name);
		CHECK(oclDamage);
		arena.logic.combat().setDamageTrace({});
	}
}
} // namespace port277real

TEST_CASE("PORT-277 real definitions: IsenfurtWchter Smite is a no-op without HordeContain") { port277real::checkRow(0); }
TEST_CASE("PORT-277 real definitions: IsenfurtWchterCreep Smite is a no-op without HordeContain") { port277real::checkRow(1); }
TEST_CASE("PORT-277 real definitions: IsenfurtWchterHorde Smite releases the spear slot") { port277real::checkRow(2); }
TEST_CASE("PORT-277 real definitions: IsenfurtWchterHordeHerold has a mismatched power before its working nugget") { port277real::checkRow(3); }
TEST_CASE("PORT-277 real definitions: DwarvenAxtschwingerHorde Axe Throw releases the axe slot") { port277real::checkRow(4); }
TEST_CASE("PORT-277 real definitions: AngmarDarkRangerHorde Ice Arrows releases the ice slot") { port277real::checkRow(5); }
TEST_CASE("PORT-277 real definitions: GondorRangerHorde Long Shot dummy fire and separate OCL damage") { port277real::checkRow(6); }
TEST_CASE("PORT-277 real definitions: DwarvenDain_mod Speerfeuer no-op rangefinder and separate OCL damage") { port277real::checkRow(7); }

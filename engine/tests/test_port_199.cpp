// OpenBFME unit tests. PORT-199 (issue #466): GiveUpgradeUpdate's contained targets, producer-linked pairs, the
// porter's deselection, the recipient's flash and the search schedule, all as RotWK 2.01 reads them. Synthetic INI, no
// retail files. Every expectation comes from the RotWK instructions named in the test or its CHECK (the give test
// RW 0x89FE64, the provider lookup RW 0x89FD77, the search RW 0x89FEBD, the delivery RW 0x8A01B2, the horde's slots
// RW 0x86ECAB / 0x871A90, the release RW 0x89FD15, the deselection RW 0x625759), never from this engine's output.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/INIDataTypes.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/GiveUpgradeUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/PartitionManager.h"

#include <string>
#include <vector>

namespace
{
const char kPort199Objects[] =
	"SpecialPower TestGiveUpgradePower\n Enum = SPECIAL_GIVE_UPGRADE\n ReloadTime = 30000\nEnd\n"
	"Object P199Porter\n KindOf = INFANTRY SELECTABLE\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AITag\n End\n"
	" Behavior = SpecialPowerModule PowerTag\n SpecialPowerTemplate = TestGiveUpgradePower\n UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = GiveUpgradeUpdate GiveTag\n SpecialPowerTemplate = TestGiveUpgradePower\n StartAbilityRange = 100\n"
	" IgnoreFacingCheck = Yes\n ApproachRequiresLOS = No\n UnpackTime = 200\n PreparationTime = 200\n PackTime = 200\n"
	" DeliverUpgrade = No\n End\n"
	"End\n"
	"Object P199Soldier\n KindOf = INFANTRY SELECTABLE\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AITag\n End\n"
	" Behavior = LocomotorSetUpgrade UpgradeTag\n TriggeredBy = Upgrade_Barracks2\n End\n"
	"End\n"
	"Object P199Horde\n KindOf = HORDE SELECTABLE\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
	" Behavior = HordeContain ContainTag\n Slots = 2\n End\n"
	" Behavior = LocomotorSetUpgrade UpgradeTag\n TriggeredBy = Upgrade_Barracks2\n End\n"
	"End\n"
	"Object P199Structure\n KindOf = STRUCTURE IMMOBILE\n"
	" Geometry = CYLINDER\n GeometryMajorRadius = 10\n GeometryHeight = 20\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 1000\n End\n"
	"End\n";

// The simulation calls these where RotWK calls the drawable: RW 0x70E013 for the flash (RW 0x8A02A3, RW 0x871B23).
struct Port199Hooks : ObjectClientHooks
{
	struct Flash
	{
		ObjectID object = INVALID_ID;
		RGBColor color{};
	};
	std::vector<Flash> flashes;
	void objectCreated(Object &) override {}
	void objectDestroyed(Object &) override {}
	void flashAsSelected(Object &obj, const RGBColor *color) override
	{
		flashes.push_back(Flash{ obj.getID(), color ? *color : RGBColor{} });
	}
};

struct Port199World : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *previous = TheSpecialPowerStore;
	explicit Port199World(bool automatic = false)
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		std::string objects = kPort199Objects;
		if (automatic)
		{
			const size_t at = objects.find(" DeliverUpgrade = No\n");
			REQUIRE(at != std::string::npos);
			objects.replace(at, std::string(" DeliverUpgrade = No\n").size(), " DeliverUpgrade = Yes\n");
		}
		const std::string error = w.load(objects);
		REQUIRE_MESSAGE(error.empty(), error);
	}
	~Port199World() { TheSpecialPowerStore = previous; }
	SpecialAbilityUpdate *porterAbility(Object *porter)
	{
		return porter ? dynamic_cast<SpecialAbilityUpdate *>(porter->findModule("GiveUpgradeUpdate")) : nullptr;
	}
};
} // namespace

TEST_CASE("PORT-199: a contained target is delivered to through its producer's horde (RW 0x89FE86 -> RW 0x89FD77, horde slot 0xAC RW 0x86ECAB, delivery RW 0x8A0243 -> slot 0xB8 RW 0x871A90)")
{
	Port199World w;
	Object *porter = w.unit("P199Porter", 'A', 300, 300);
	Object *horde = w.unit("P199Horde", 'A', 305, 300);
	Object *member = w.unit("P199Soldier", 'A', 306, 300);
	REQUIRE(porter);
	REQUIRE(horde);
	REQUIRE(member);
	REQUIRE(horde->getContain());
	REQUIRE(horde->getContain()->addToContain(member)); // the member sits in the horde: Object + 0x27C is the horde
	member->setProducer(horde);                         // Object + 0x78: the horde made it (RW 0x89FD7F)
	REQUIRE(member->getContainedBy() == horde);

	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_Barracks2");
	REQUIRE(upgrade);
	REQUIRE(member->affectedByUpgrade(upgrade)); // the provider's own answer (RW 0x86ECCF -> RW 0x694914)
	porter->giveUpgrade(upgrade);                // what the porter carries (RW 0x89FE67 reads the object's mask)

	SpecialAbilityUpdate *ability = w.porterAbility(porter);
	REQUIRE(ability);
	ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, member, nullptr, 0, 0);
	w.frames(30);

	CHECK(horde->hasUpgrade(upgrade));     // RW 0x871AE5: the horde object takes the upgrade
	CHECK(member->hasUpgrade(upgrade));    // RW 0x871B08: its member that is affectedByUpgrade takes it too
	CHECK_FALSE(member->testStatus(0x42)); // RW 0x89FD38: the reservation clears on the target
}

TEST_CASE("PORT-199: a producer-linked target reserves the producer with itself and delivers into the producer's horde (RW 0x8A002C / RW 0x8A0037, RW 0x89FD77)")
{
	Port199World w;
	Object *porter = w.unit("P199Porter", 'A', 300, 300);
	Object *horde = w.unit("P199Horde", 'A', 320, 300);
	Object *member = w.unit("P199Soldier", 'A', 321, 300);
	Object *linked = w.unit("P199Soldier", 'A', 305, 300);
	REQUIRE(porter);
	REQUIRE(horde);
	REQUIRE(member);
	REQUIRE(linked);
	REQUIRE(horde->getContain());
	REQUIRE(horde->getContain()->addToContain(member));
	linked->setProducer(horde); // a producer that is not a structure (Thing + 0x108 bit 0x80 is clear)
	REQUIRE(linked->getContainedBy() == nullptr);

	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_Barracks2");
	REQUIRE(upgrade);
	porter->giveUpgrade(upgrade);
	SpecialAbilityUpdate *ability = w.porterAbility(porter);
	REQUIRE(ability);
	ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, linked, nullptr, 0, 0);

	// The search reserves the pair before the ability starts (RW 0x8A0029 sets the linked object, RW 0x8A0035 the
	// candidate), so the reservation is there the moment the order lands.
	CHECK(linked->testStatus(0x42));
	CHECK(horde->testStatus(0x42));

	w.frames(30);
	CHECK(horde->hasUpgrade(upgrade));        // RW 0x8A0243 -> RW 0x89FD77 -> slot 0xB8
	CHECK(member->hasUpgrade(upgrade));       // RW 0x871B08: the horde passes it to its affected members
	CHECK_FALSE(linked->hasUpgrade(upgrade)); // the upgrade lands in the producer's horde, not on the linked object
}

TEST_CASE("PORT-199: the delivering porter leaves the selection and the recipient flashes (RW 0x8A02A8 -> RW 0x625759, RW 0x8A02A3 -> RW 0x675749, colour RW 0xBE53BC)")
{
	Port199World w;
	Port199Hooks hooks;
	ObjectClientHooks *previous = w.logic->clientHooks();
	w.logic->setClientHooks(&hooks);

	Object *porter = w.unit("P199Porter", 'A', 300, 300);
	Object *structure = w.unit("P199Structure", 'A', 330, 300);
	Object *target = w.unit("P199Soldier", 'A', 305, 300);
	REQUIRE(porter);
	REQUIRE(structure);
	REQUIRE(target);
	target->setProducer(structure); // a structure producer: no linked reservation, the target takes it itself

	Player *alice = w.playerOf('A');
	REQUIRE(alice);
	alice->selection().clear();
	alice->selection().push_back(porter->getID());
	const ObjectID porterID = porter->getID();

	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_Barracks2");
	REQUIRE(upgrade);
	porter->giveUpgrade(upgrade);
	SpecialAbilityUpdate *ability = w.porterAbility(porter);
	REQUIRE(ability);
	ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, target, nullptr, 0, 0);
	w.frames(30);

	CHECK(target->hasUpgrade(upgrade)); // RW 0x8A0287 -> RW 0x69388B
	bool porterSelected = false;
	for (const ObjectID id : alice->selection()) porterSelected = porterSelected || id == porterID;
	CHECK_FALSE(porterSelected); // RW 0x8A02A8: the porter leaves every player's selection (mask 0xFFFFF)

	REQUIRE(hooks.flashes.size() == 1);
	CHECK(hooks.flashes[0].object == target->getID()); // RW 0x8A028E: the recipient's drawable (RW 0x70E013)
	// RW 0xBE53BC is the same float in all three components: 0x3F7D70A4 is 0.99f exactly, so the whole colour compares exactly.
	CHECK(hooks.flashes[0].color.red == 0.99f);
	CHECK(hooks.flashes[0].color.green == 0.99f);
	CHECK(hooks.flashes[0].color.blue == 0.99f);
	w.logic->setClientHooks(previous);
}

TEST_CASE("PORT-199: the automatic search wakes every frame, takes the nearest eligible ally and skips an enemy (RW 0x89FEBD, RW 0x8A0100, RW 0x8A01A1)")
{
	Port199World w(true); // DeliverUpgrade = Yes: the porter searches on its own, no order is given
	Object *porter = w.unit("P199Porter", 'A', 300, 300);
	Object *horde = w.unit("P199Horde", 'A', 310, 300);
	Object *member = w.unit("P199Soldier", 'A', 311, 300);
	Object *far = w.unit("P199Soldier", 'A', 380, 300);
	Object *enemy = w.unit("P199Soldier", 'B', 302, 300);
	REQUIRE(porter);
	REQUIRE(horde);
	REQUIRE(member);
	REQUIRE(far);
	REQUIRE(enemy);
	REQUIRE(horde->getContain());
	REQUIRE(horde->getContain()->addToContain(member));
	member->setProducer(horde);
	far->setProducer(horde);

	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_Barracks2");
	REQUIRE(upgrade);
	porter->giveUpgrade(upgrade);
	REQUIRE(porter->hasUpgrade(upgrade));
	w.logic->partition().update();
	w.frames(30); // RW 0x8A01A1: a searching porter wakes every frame

	CHECK(horde->hasUpgrade(upgrade));      // the nearer eligible target's horde
	CHECK_FALSE(far->hasUpgrade(upgrade));  // near to far (RW 0xC10E20 / RW 0xC1676C): the far one never comes up
	CHECK_FALSE(enemy->hasUpgrade(upgrade)); // RW 0xC0F374: the owner's relationship filter
}

// OpenBFME. GPL-3.0.
// PORT-165 (issue #394): the member-attack wait state of RW 0x74548D.
#include "GameLogic/AI/AIAttackHordePosition.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <stdexcept>

AIHordePositionWaitState::AIHordePositionWaitState(AIStateMachine &machine)
	: AIState(machine, "AIHordePositionWaitState")
{
}

StateReturnType AIHordePositionWaitState::onEnter()
{
	// RW 0x747999: KindOf HORDE (+0x114 mask 0x2000) returns 0, otherwise -2.
	return owner().isKindOfName("HORDE") ? STATE_CONTINUE : STATE_FAILURE;
}

StateReturnType AIHordePositionWaitState::update()
{
	// RW 0x742CEA -> 0x742C8C: a dead goal fails; otherwise the horde's
	// interface is asked whether every living AI member attacks the goal (slot
	// 0x148), or is active when there is no goal (slot 0x14C).
	Object *goal = machine().goalObject();
	if (goal && goal->isEffectivelyDead())
	{
		return STATE_FAILURE;
	}
	ContainModuleInterface *contain = owner().getContain();
	if (!contain || !contain->getHordeContainInterface() || !contain->getContainedItemsList())
	{
		throw std::logic_error("AIHordePositionWaitState: HORDE has no horde containment interface");
	}
	const auto members = *contain->getContainedItemsList(); // RW 0x865598 takes a copy
	for (const Object *member : members)
	{
		if (!member || member->isEffectivelyDead())
		{
			continue;
		}
		const AIUpdateInterface *memberAI = member->getAIUpdateInterface();
		if (!memberAI)
		{
			continue;
		}
		// RW 0x86D562 / 0x86D438 query AI vslot 0x1BC (active, not
		// merely the attack-state id). An empty eligible member list passes.
		if (!memberAI->isStateActive())
		{
			return STATE_FAILURE;
		}
		if (goal)
		{
			Object *victim = memberAI->currentVictim(); // RW 0x668303
			if (!victim || victim->getID() != goal->getID())
			{
				return STATE_FAILURE;
			}
			if (goal->isKindOfName("HORDE"))
			{
				ContainModuleInterface *targetContain = goal->getContain();
				if (!targetContain || !targetContain->getContainedItemsList())
				{
					throw std::logic_error("AIHordePositionWaitState: target HORDE has no containment list");
				}
				const auto &targetMembers = *targetContain->getContainedItemsList();
				if (std::find(targetMembers.begin(), targetMembers.end(), victim) == targetMembers.end())
				{
					return STATE_FAILURE; // RW 0x86D4A4: target contain vslot 0xE8
				}
			}
		}
	}
	return STATE_CONTINUE;
}

// RW 0x74C911 / 0x74C9FF / 0x74C8BE: the position variant shares
// the exit routine with the ordinary fire state, but has its own entry and
// update. Its +0x24 deferred flag is simulation state and is saved below.
AIHordePositionFireState::AIHordePositionFireState(AIStateMachine &machine, AttackFireNotify &notify)
	: AIState(machine, "AIHordePositionFireState"), m_notify(&notify)
{
}

StateReturnType AIHordePositionFireState::onEnter()
{
	Object &obj = owner();
	m_deferred = false;
	obj.setStatus((unsigned)CombatNames::status("END_FIRE_STATE"), false);
	if ((ai().moodAdjustment(2) & 1u) == 0)
	{
		return STATE_FAILURE;
	}
	ObjectWeapons *weapons = obj.getWeapons();
	Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr;
	if (!weapon || !weapon->getTemplate())
	{
		return STATE_FAILURE;
	}
	const WeaponTemplate &tmpl = *weapon->getTemplate();
	// RW 0x74C95E: an immobile owner, or a moving-fire / melee weapon,
	// bypasses the ordinary moving-owner refusal.
	if (!obj.isKindOfName("IMMOBILE") && !tmpl.m_canFireWhileMoving && !tmpl.m_meleeWeapon && ai().isMoving())
	{
		return STATE_FAILURE;
	}
	if (!weapons->isWithinAttackRange(machine().goalPosition()))
	{
		return STATE_FAILURE;
	}
	if (weapons->weaponStatusWriteBack(*weapon) != WEAPON_READY_TO_FIRE)
	{
		return STATE_SUCCESS;
	}
	if (obj.testStatus((unsigned)CombatNames::statuses().hordeMember) && (ai().frame() & 1u))
	{
		m_deferred = true;
		return STATE_CONTINUE;
	}
	obj.setStatus((unsigned)CombatNames::statuses().isFiringWeapon, true);
	weapons->preFireCurrentWeapon(machine().goalObject(), &machine().goalPosition());
	return STATE_CONTINUE;
}

StateReturnType AIHordePositionFireState::update()
{
	Object &obj = owner();
	ObjectWeapons *weapons = obj.getWeapons();
	Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr;
	const int endFire = CombatNames::status("END_FIRE_STATE");
	if (!weapon || obj.isEffectivelyDead() || obj.testStatus((unsigned)endFire))
	{
		return STATE_FAILURE;
	}
	if (m_deferred)
	{
		m_deferred = false;
		obj.setStatus((unsigned)CombatNames::statuses().isFiringWeapon, true);
		weapons->preFireCurrentWeapon(machine().goalObject(), &machine().goalPosition());
		return STATE_CONTINUE;
	}
	const int status = weapons->weaponStatusWriteBack(*weapon);
	if (status == WEAPON_PRE_ATTACK)
	{
		return STATE_CONTINUE;
	}
	if (status != WEAPON_READY_TO_FIRE || !m_notify->isWeaponSlotOkToFire(weapons->curSlot()))
	{
		return STATE_FAILURE;
	}
	weapons->setFiringConditionForCurrentWeapon(); // RW 0x69036C
	weapons->fireCurrentWeapon(nullptr, &machine().goalPosition()); // RW 0x6920AD
	obj.setStatus((unsigned)CombatNames::status("IGNORING_STEALTH"), false);
	m_notify->notifyFired();
	return STATE_SUCCESS;
}

void AIHordePositionFireState::onExit(StateExitType)
{
	if (ai().isDestroying())
	{
		return;
	}
	Object &obj = owner();
	obj.setStatus((unsigned)CombatNames::statuses().isFiringWeapon, false);
	obj.setStatus((unsigned)CombatNames::status("IGNORING_STEALTH"), false);
	ObjectWeapons *weapons = obj.getWeapons();
	if (Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr)
	{
		if (weapons->weaponStatusWriteBack(*weapon) == WEAPON_PRE_ATTACK)
		{
			weapon->setWhenPreAttackFinished(0);
			weapons->weaponStatusWriteBack(*weapon);
		}
	}
	obj.setStatus((unsigned)CombatNames::status("END_FIRE_STATE"), false);
}

void AIHordePositionFireState::crc(StateHasher &hasher) const
{
	hasher.addBool(m_deferred);
}

void AIHordePositionFireState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&m_deferred);
}

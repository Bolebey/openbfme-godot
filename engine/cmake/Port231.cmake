# PORT-231 (issue #554): building, producing, earning layer
# - GameData MaxCastleRadius (RW 0x00796D37 / 0x00661A02 / 0x00661C13) and BuildPlacement::isLocationClearOfObjects 3D geometry overlap
# - GameData SellPercentage ctor default 1.0f (RW 0x00643160 -> 0x00643222, S-1981)
# - CastleMemberBehavior::BeingBuiltSound (RW 0x00799F0F -> 0x00798057 -> 0x00797C40, HELD_CASTLE_BEING_BUILT = 5)
# - ProductionSpeedBonus -> Player -> BuildAssistant::calcTimeToBuild & ProductionUpdate (RW 0x008C7171 -> 0x006AF3C8 / 0x006AF484 / 0x006AF28E, 0x0073C3E6, 0x008A1C1C, HELD_PRODUCTION_SPEED_BONUS = 6)
# - ProductionUpdate::completeUnit script naming (RW 0x008A253B / 0x008A2624) and hordeTarget migration (RW 0x008A2A67 -> 0x008A171F -> 0x008A07E0)
# - Object::attemptHealingFromSoleBenefactor KeepSelectableWhenDead tail (RW 0x00690584 -> 0x00690626, S-652)

set(OPENBFME_PORT231_TESTS
    tests/test_port_231.cpp)

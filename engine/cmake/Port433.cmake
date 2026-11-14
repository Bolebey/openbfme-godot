# PORT-433 (issue #433): Ithilien grove (ElvenWood) area bonuses must apply to player units standing in the
# painted Elven Wood / Taint zone even though the grove itself belongs to the neutral player.
# RW: paintArea 0x8C7E89 / 0x8C8F22; AttributeModifierAuraUpdate::update ally-filter skip 0x89F605..0x89F62A;
#     RequiredConditions owner check 0x89F200..0x89F271.
set(OPENBFME_PORT433_TESTS tests/test_port_433.cpp)

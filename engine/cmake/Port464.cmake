# PORT-464 (issue #464, the parked Port 197): the per-frame steering of a summoned helper — slot 1 of the AISpecialPowerUpdate decision
# (TARGETAOE_SUMMON RW 0x9EB961). The engine change is in GameLogic/Module/AISpecialPowerUpdate.cpp and
# GameLogic/Module/SpecialPowerModules.cpp, which the existing source lists carry.

set(OPENBFME_PORT464_TESTS
    tests/test_port_464.cpp)

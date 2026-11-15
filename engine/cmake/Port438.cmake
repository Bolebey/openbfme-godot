# PORT-438 (fork issue 438, P-2): the forbidden-objects test of a spell book summon (ActionManager canDoSpecialPowerAtLocation RW 0x82D2FD) measures to the
# object's bounding circle in two dimensions (RW distance type 1 = RW 0xA3AE50), not to the centre in three dimensions. The lane's engine change is in a
# file other lists already carry (GameLogic/ActionManager).

set(OPENBFME_PORT438_TESTS
    tests/test_port_438.cpp)

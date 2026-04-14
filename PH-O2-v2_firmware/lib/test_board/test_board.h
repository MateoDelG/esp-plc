#ifndef TEST_BOARD_H
#define TEST_BOARD_H


namespace TestBoard {
    void testPumps(int pump1Pin, int pump2Pin, int pump3Pin, int pump4Pin, int mixerPin);
    void testBuzzer(int buzzerPin, int durationMs);
    void testButtons();
}

#endif // TEST_BOARD_H
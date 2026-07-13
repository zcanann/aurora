#include <dolphin/os.h>

#include <gtest/gtest.h>

#include <atomic>
#include <limits>
#include <thread>
#include <vector>

namespace {

class DeterministicTimeTest : public testing::Test {
protected:
    void TearDown() override { AuroraDisableDeterministicTime(); }
};

TEST_F(DeterministicTimeTest, RealtimeRemainsTheDefault) {
    EXPECT_FALSE(AuroraIsDeterministicTimeEnabled());
    const OSTime first = OSGetTime();
    const OSTime second = OSGetTime();
    EXPECT_GE(second, first);
}

TEST_F(DeterministicTimeTest, ThirtyStepsAreExactlyOneSecond) {
    constexpr OSTime initial = 123456789;
    ASSERT_TRUE(AuroraEnableDeterministicTime(initial, 30, 1));
    EXPECT_EQ(OSGetTime(), initial);
    EXPECT_EQ(OSGetTick(), static_cast<OSTick>(initial));

    ASSERT_TRUE(AuroraAdvanceDeterministicTime(30));
    EXPECT_EQ(OSGetTime(), initial + OS_TIMER_CLOCK);
}

TEST_F(DeterministicTimeTest, RationalRemainderDoesNotDrift) {
    // 7/3 Hz is deliberately not an integer number of timer ticks per update.
    // After seven updates exactly three seconds must have elapsed.
    ASSERT_TRUE(AuroraEnableDeterministicTime(0, 7, 3));
    ASSERT_TRUE(AuroraAdvanceDeterministicTime(7));
    EXPECT_EQ(OSGetTime(), static_cast<OSTime>(OS_TIMER_CLOCK) * 3);
}

TEST_F(DeterministicTimeTest, ResetAlsoResetsFractionalPhase) {
    ASSERT_TRUE(AuroraEnableDeterministicTime(10, 7, 1));
    ASSERT_TRUE(AuroraAdvanceDeterministicTime(1));
    const OSTime firstStep = OSGetTime() - 10;

    ASSERT_TRUE(AuroraResetDeterministicTime(100));
    ASSERT_TRUE(AuroraAdvanceDeterministicTime(1));
    EXPECT_EQ(OSGetTime() - 100, firstStep);
}

TEST_F(DeterministicTimeTest, OverflowFailsWithoutChangingTime) {
    const OSTime initial = std::numeric_limits<OSTime>::max() - 1;
    ASSERT_TRUE(AuroraEnableDeterministicTime(initial, 30, 1));
    EXPECT_FALSE(AuroraAdvanceDeterministicTime(1));
    EXPECT_EQ(OSGetTime(), initial);
}

TEST_F(DeterministicTimeTest, ConcurrentReadsSeeOnlyLogicalTime) {
    ASSERT_TRUE(AuroraEnableDeterministicTime(0, 30, 1));
    std::atomic<bool> run{true};
    std::atomic<bool> invalid{false};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            OSTime previous = 0;
            while (run.load(std::memory_order_acquire)) {
                const OSTime current = OSGetTime();
                if (current < previous || current % (OS_TIMER_CLOCK / 30) != 0) {
                    invalid.store(true, std::memory_order_release);
                }
                previous = current;
            }
        });
    }
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(AuroraAdvanceDeterministicTime(1));
    }
    run.store(false, std::memory_order_release);
    for (std::thread& reader : readers) {
        reader.join();
    }
    EXPECT_FALSE(invalid.load(std::memory_order_acquire));
    EXPECT_EQ(OSGetTime(), static_cast<OSTime>(OS_TIMER_CLOCK / 30) * 1000);
}

} // namespace

#include <aurora/aurora.h>
#include <dolphin/ar.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace aurora {
extern AuroraConfig g_config;
}

namespace {
std::array<u32, 16> gBlockLengths{};
}

TEST(ARState, RestoresAllocatorCursorAndBlockStack) {
  aurora::g_config.mem2Size = 1024 * 1024;
  ASSERT_EQ(ARInit(gBlockLengths.data(), gBlockLengths.size()), 0x4000u);

  EXPECT_EQ(ARAlloc(64), 0x4000u);
  AuroraARState snapshot{};
  ASSERT_TRUE(ARCaptureState(&snapshot));

  EXPECT_EQ(ARAlloc(96), 0x4040u);
  ASSERT_TRUE(ARRestoreState(&snapshot));
  EXPECT_EQ(ARAlloc(32), 0x4040u);
}

TEST(ARState, RejectsMalformedStateWithoutMovingCursor) {
  AuroraARState before{};
  ASSERT_TRUE(ARCaptureState(&before));
  AuroraARState malformed = before;
  malformed.stackPointer = aurora::g_config.mem2Size + 1;
  EXPECT_FALSE(ARRestoreState(&malformed));

  AuroraARState after{};
  ASSERT_TRUE(ARCaptureState(&after));
  EXPECT_EQ(after.stackPointer, before.stackPointer);
  EXPECT_EQ(after.freeBlocks, before.freeBlocks);
  EXPECT_EQ(after.blockLengthAddress, before.blockLengthAddress);
}

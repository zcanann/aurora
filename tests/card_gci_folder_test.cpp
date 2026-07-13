#include "card/CardGciFolder.hpp"

#include <aurora/aurora.h>
#include <gtest/gtest.h>

namespace aurora {

AuroraConfig g_config{};

void log_internal(AuroraLogLevel, const char*, const char*, unsigned int) noexcept {}

} // namespace aurora

namespace aurora::card {
namespace {

TEST(CardGciFolderTest, MissingFileByNameReturnsNoFile) {
  CardGciFolder card;
  FileHandle handle;

  EXPECT_EQ(card.openFile("missing", handle), ECardResult::NOFILE);
}

TEST(CardGciFolderTest, MissingFileByIndexReturnsNoFile) {
  CardGciFolder card;
  FileHandle handle;

  EXPECT_EQ(card.openFile(uint32_t{0}, handle), ECardResult::NOFILE);
}

} // namespace
} // namespace aurora::card

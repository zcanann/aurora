#pragma once

#include "../internal.hpp"
#include <aurora/gfx.h>

namespace aurora::gx::fifo {

// Process a buffer of GX FIFO commands
void process(const uint8_t* data, uint32_t size, bool bigEndian);

void get_channel_count_telemetry(AuroraGXChannelCountTelemetry& outTelemetry);
void reset_channel_count_telemetry();

} // namespace aurora::gx::fifo

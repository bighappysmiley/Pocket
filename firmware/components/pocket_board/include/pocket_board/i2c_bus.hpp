#pragma once
#include "driver/i2c_master.h"

namespace pocket::board {

/** Shared I2C master on GPIO41/42 (AXP2101 + ES8311). Creates bus on first use. */
i2c_master_bus_handle_t i2c_bus();

}  // namespace pocket::board

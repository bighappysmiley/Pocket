#pragma once

namespace pocket::board {

/**
 * Bring up AXP2101 (board’s “TG28”) rails used by the 3.97" e-Paper.
 * Schematic net EPD_VCC is fed from EPD_VCC_AXP (ALDOs). Without this, a cold
 * USB plug-in can leave the panel unpowered so SPI refreshes do nothing and
 * the latched factory image stays visible.
 *
 * I2C: SDA=GPIO41, SCL=GPIO42, addr 0x34 (Waveshare ESP32-S3-ePaper-3.97).
 * Safe no-op if the PMIC is absent or already configured.
 */
bool axp_enable_epd_rails();

}  // namespace pocket::board

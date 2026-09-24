#pragma once
/**
 * Waveshare ESP32-S3-ePaper-3.97 e-Paper port (adapted from official
 * waveshareteam/ESP32-S3-ePaper-3.97 ESP-IDF demo, MIT license).
 */
#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t UBYTE;
typedef uint16_t UWORD;
typedef uint32_t UDOUBLE;

#define EPD_SCLK_PIN 11
#define EPD_MOSI_PIN 12
#define EPD_CS_PIN 10
#define EPD_DC_PIN 9
#define EPD_RST_PIN 46
#define EPD_BUSY_PIN 3

#define epaper_rst_1 gpio_set_level(EPD_RST_PIN, 1)
#define epaper_rst_0 gpio_set_level(EPD_RST_PIN, 0)
#define epaper_cs_1 gpio_set_level(EPD_CS_PIN, 1)
#define epaper_cs_0 gpio_set_level(EPD_CS_PIN, 0)
#define epaper_dc_1 gpio_set_level(EPD_DC_PIN, 1)
#define epaper_dc_0 gpio_set_level(EPD_DC_PIN, 0)
#define ReadBusy gpio_get_level(EPD_BUSY_PIN)

#define EPD_WIDTH 800
#define EPD_HEIGHT 480
#define EPD_SIZE_MONO 48000
#define EPD_SIZE_4GRAY 96000

void epaper_port_init(void);
void EPD_Init(void);
void EPD_Init_Fast(void);
void EPD_Clear(void);
void EPD_Clear_Black(void);
void EPD_Display(const UBYTE* Image);
void EPD_Display_Base(const UBYTE* Image);
void EPD_Display_Fast(const UBYTE* Image);
void EPD_Sleep(void);

#ifdef __cplusplus
}
#endif

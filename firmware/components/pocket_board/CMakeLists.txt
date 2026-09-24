idf_component_register(
  SRCS
    "epd.cpp"
    "buttons.cpp"
    "axp.cpp"
    "i2c_bus.cpp"
    "sdcard.cpp"
    "audio.cpp"
    "vendor/waveshare_epd.c"
  INCLUDE_DIRS "include" "vendor"
  REQUIRES pocket_ui driver freertos esp_timer fatfs sdmmc
)

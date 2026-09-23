idf_component_register(
  SRCS
    "epd.cpp"
    "buttons.cpp"
  INCLUDE_DIRS "include"
  REQUIRES pocket_ui driver freertos esp_timer
)

idf_component_register(
  SRCS "app_main.cpp" "esp_wifi_platform.cpp" "esp_cloud_platform.cpp"
  INCLUDE_DIRS "."
  REQUIRES pocket_ui pocket_board nvs_flash esp_wifi esp_netif esp_event esp_http_client esp_http_server esp-tls mbedtls lwip
)

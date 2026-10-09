FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "hal" "system" "ui" "screens"
                       REQUIRES esp_partition app_update esp_app_format esp_timer nvs_flash
                                esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_driver_sdspi
                                esp_driver_spi esp_driver_uart esp_adc fatfs sdmmc esp_wifi esp_netif esp_event
                                lwip esp_hw_support esp_pm ulp esp_http_client mbedtls bootloader_support)

# Os snprintf truncam de propósito (nomes longos em buffers fixos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation -Wshadow -Wno-error=shadow)

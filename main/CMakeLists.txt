idf_component_register(SRCS "main.c" "http_server.c" "lights_controller.c" "can_bus_sniffer.c"
                        REQUIRES driver
                        REQUIRES esp_http_server
                        REQUIRES esp_wifi
                        REQUIRES nvs_flash
                        INCLUDE_DIRS ".")
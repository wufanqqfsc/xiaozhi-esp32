idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash lvgl__lvgl waveshare__esp32_s3_touch_lcd_1_85B
)


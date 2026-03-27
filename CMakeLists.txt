set(COMPONENT_SRCS "dac_stream.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")

set(COMPONENT_REQUIRES audio_pipeline esp_driver_gpio esp_driver_dac)

register_component()

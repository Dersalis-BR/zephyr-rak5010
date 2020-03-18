#
# Copyright (c) 2020 Dersalis
#
#

#zephyr_include_directories(.)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modem_iface_uart.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modem_cmd_handler.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modem_context.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modem_pin.c)
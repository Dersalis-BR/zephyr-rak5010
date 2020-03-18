#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#
cmake_minimum_required(VERSION 3.8.2)

include($ENV{ZEPHYR_BASE}/../nrf/cmake/boilerplate.cmake)
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(dsbox_gateway)

# DERSALIS DS-BOX APP START
target_sources(app PRIVATE src/main.c)

# Include application events and configuration headers
zephyr_library_include_directories(     
    src/libraries/lightning  
    src/libraries/net/at_cmd_parser
    src/libraries/net/net_lc
    src/libraries/net/mqtt_lc
    src/drivers/modem
)

# Application sources
add_subdirectory(src/libraries/lightning)
add_subdirectory(src/libraries/net/at_cmd_parser)
add_subdirectory(src/libraries/net/net_lc)
add_subdirectory(src/libraries/net/mqtt_lc)
add_subdirectory(src/drivers/modem)
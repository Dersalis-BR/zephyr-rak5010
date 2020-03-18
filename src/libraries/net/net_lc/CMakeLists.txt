#
# Copyright (c) 2020 Dersalis
#
#

zephyr_include_directories(.)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/net_lc.c)
﻿# CMake 最低版本号要求
cmake_minimum_required (VERSION 2.8)

aux_source_directory(. DIR_STRUCTS_SRCS)

file(GLOB_RECURSE CURRENT_HEADERS  *.h *.hpp)
source_group("Include" FILES ${CURRENT_HEADERS})

add_library(panutils_utils ${DIR_STRUCTS_SRCS} ${CURRENT_HEADERS})
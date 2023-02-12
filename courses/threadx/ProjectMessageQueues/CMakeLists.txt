# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.13..3.22 FATAL_ERROR)
set(CMAKE_C_STANDARD 99)

# Define paths
set(BASE_DIR ${CMAKE_SOURCE_DIR}/../../..)
set(LIBS_DIR ${BASE_DIR}/libs)

include(${BASE_DIR}/cmake/linux.cmake)

# Project
set(PROJECT ProjectMessageQueues)
project(${PROJECT} VERSION 0.1.0 LANGUAGES C CXX ASM)

add_subdirectory(${LIBS_DIR}/threadx lib)

add_executable(${PROJECT} main.c)

target_link_libraries(${PROJECT} PUBLIC azrtos::threadx)

# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.13..3.22 FATAL_ERROR)
set(CMAKE_C_STANDARD 99)

# Define paths
set(BASE_DIR ${CMAKE_SOURCE_DIR}/../../..)
set(LIBS_DIR ${BASE_DIR}/libs)

include(${BASE_DIR}/cmake/linux.cmake)

if(NOT DEFINED IF_NAME)
  set(IF_NAME "veth1")
endif()
message(STATUS "Bind to interface: ${IF_NAME}")

set(NX_USER_FILE ${CMAKE_SOURCE_DIR}/nx_user.h)

set(NXD_ENABLE_FILE_SERVERS
    OFF
    CACHE BOOL "Includes a dependency on FileX to support 'server' protocol handlers (default is ON)")

# Project
set(PROJECT ProjectUDPEchoClient)
project(${PROJECT} VERSION 0.1.0 LANGUAGES C)

add_subdirectory(${LIBS_DIR}/threadx lib/threadx)
add_subdirectory(${LIBS_DIR}/netxduo lib/netxduo)

add_executable(${PROJECT} main.c ../Driver/nx_linux_network_driver.c)

target_link_libraries(${PROJECT} PUBLIC azrtos::netxduo)
target_compile_definitions(${PROJECT} PUBLIC -DNX_LINUX_INTERFACE_NAME=\"${IF_NAME}\")

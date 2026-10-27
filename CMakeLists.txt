cmake_minimum_required(VERSION 3.16)

# Keep the ESP-IDF app in Server/main while allowing builds from the repo root.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/Server/main")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(INT3108_Smart_Parking_Project)

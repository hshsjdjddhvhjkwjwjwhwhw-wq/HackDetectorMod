cmake_minimum_required(VERSION 3.25)
project(HackDetector VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Geode REQUIRED)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

setup_geode_mod(${PROJECT_NAME})

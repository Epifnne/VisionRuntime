cmake_minimum_required(VERSION 3.25)

project(VisonRuntime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/gtest/1.17.0 EXCLUDE_FROM_ALL)
add_subdirectory(Runtime/tests)

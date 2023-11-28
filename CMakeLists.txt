cmake_minimum_required(VERSION 3.14)
project(cotry
  VERSION 1.0.1
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

add_executable(test test/main.cpp)
target_include_directories(test PUBLIC include)

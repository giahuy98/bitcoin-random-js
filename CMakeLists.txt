cmake_minimum_required(VERSION 3.20)

project(bitcoin-random-js LANGUAGES CXX)

add_library(bitcoin_random STATIC random.cpp)
target_include_directories(bitcoin_random PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(bitcoin_random PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(bitcoin_random PRIVATE /W4)
else()
  target_compile_options(bitcoin_random PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(bitcoin_random_example example.cpp)
target_link_libraries(bitcoin_random_example PRIVATE bitcoin_random)

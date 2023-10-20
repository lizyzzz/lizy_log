cmake_minimum_required(VERSION 3.0)

project(lizy_log)

add_compile_options(-g -std=c++14)

include_directories(./include)

# 生成动态链接库
add_library(lizyLog SHARED 
  src/logging.cc
  src/utilities.cc
)

add_executable(test test.cpp)
target_link_libraries(test
  lizyLog
)
cmake_minimum_required(VERSION 3.0)

project(lizy_log)

add_compile_options(-g -std=c++17)

include_directories(./include)

set(SOURCES
  ./src/logging.cc
  ./src/utilities.cc
)

# 生成动态链接库
add_library(lizyLog SHARED 
  ${SOURCES}
)

add_executable(test
  test.cpp
)

target_link_libraries(test
  lizyLog
)

# 指定安装目录
install(TARGETS lizyLog DESTINATION /usr/local/lib/lizyLog/)
install(DIRECTORY include/ DESTINATION /usr/local/include/lizyLog)

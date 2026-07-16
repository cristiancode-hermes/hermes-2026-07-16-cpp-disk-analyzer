cmake_minimum_required(VERSION 3.28)
project(disk_analyzer_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Catch2 already fetched by parent CMakeLists.txt
add_executable(disk_analyzer_test test_main.cpp)
target_include_directories(disk_analyzer_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(disk_analyzer_test PRIVATE
  Catch2::Catch2WithMain
  disk-analyzer-lib
  nlohmann_json::nlohmann_json
  spdlog::spdlog
)

include(CTest)
add_test(NAME disk_analyzer_test COMMAND disk_analyzer_test)

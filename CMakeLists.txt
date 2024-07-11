cmake_minimum_required(VERSION 3.28)
project(EXRHeader)

set(CMAKE_CXX_STANDARD 17)

# Add this line to find the PNG library
find_package(PNG REQUIRED)

add_executable(EXRHeader main.cpp)

# Link the PNG library
target_link_libraries(EXRHeader PNG::PNG)
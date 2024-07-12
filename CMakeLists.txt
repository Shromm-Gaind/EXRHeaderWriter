cmake_minimum_required(VERSION 3.28)
project(EXRHeader)

set(CMAKE_CXX_STANDARD 17)

# Add this line to find the PNG library
find_package(PNG REQUIRED)
find_package(OpenCV REQUIRED)

add_executable(EXRHeader main.cpp)

# Include OpenCV directories
include_directories(${OpenCV_INCLUDE_DIRS})

# Link the PNG library
target_link_libraries(EXRHeader PNG::PNG)
target_link_libraries(EXRHeader ${OpenCV_LIBS})
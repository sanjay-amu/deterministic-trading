# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(DeterministicTrading CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags for ARM64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=armv8.5-a -flto -fno-exceptions -fno-rtti -DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -fno-exceptions -fno-rtti -DNDEBUG")
endif()

# Main benchmark executable
add_executable(benchmark benchmark.cpp)

# Find threads library
find_package(Threads REQUIRED)
target_link_libraries(benchmark PRIVATE Threads::Threads)

# macOS specific
if(APPLE)
    target_compile_definitions(benchmark PRIVATE __APPLE__)
endif()

# Print configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CXX Flags: ${CMAKE_CXX_FLAGS_RELEASE}")

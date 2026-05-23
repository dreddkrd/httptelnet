cmake_minimum_required(VERSION 3.15)
project(httptelnet CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -Wextra -O2")

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/include)

# Source files
set(SOURCES
    src/main.cpp
    src/telnet_client.cpp
    src/device_manager.cpp
    src/http_server.cpp
    src/request_handler.cpp
)

# Create executable
add_executable(httptelnet ${SOURCES})

# Link pthreads
find_package(Threads REQUIRED)
target_link_libraries(httptelnet Threads::Threads)

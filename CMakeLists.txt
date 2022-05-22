cmake_minimum_required(VERSION 3.15)
project(linux_webserver VERSION 0.1)


SET(CXX_STANDARD 11)

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
SET(LIB_NAME Reactor)
SET(THREADS_PREFER_PTHREAD_FLAG ON)
FILE(GLOB SRC "${PROJECT_SOURCE_DIR}/src/*.cpp")


include_directories(${PROJECT_SOURCE_DIR}/include)


add_library(${LIB_NAME} STATIC ${SRC})
add_executable(${PROJECT_NAME} ${PROJECT_SOURCE_DIR}/main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} ${LIB_NAME} Threads::Threads)
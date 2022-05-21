cmake_minimum_required(VERSION 3.15)
project(linux_webserver VERSION 0.1)


SET(CXX_STANDARD 11)

SET(RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
SET(LIB_NAME Reactor)
FILE(GLOB SRC "${PROJECT_SOURCE_DIR}/src/*.cpp")


include_directories(${PROJECT_SOURCE_DIR}/include)


add_library(${LIB_NAME} STATIC ${SRC})
add_executable(${PROJECT_NAME} ${PROJECT_SOURCE_DIR}/main.cpp)
link_directories(${LIBRARY_OUTPUT_PATH})
target_link_libraries(${PROJECT_NAME} ${LIB_NAME} -lpthread)




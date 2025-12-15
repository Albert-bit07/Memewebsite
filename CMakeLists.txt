cmake_minimum_required(VERSION 3.15)
project(cpp_recommender)
set(CMAKE_CXX_STANDARD 17)

set(Python3_ROOT_DIR "C:/Program Files/Python310")
find_package(Python3 3.10 EXACT COMPONENTS Interpreter Development REQUIRED)

message(STATUS "Python3 executable: ${Python3_EXECUTABLE}")
message(STATUS "Python3 version: ${Python3_VERSION}")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import pybind11; print(pybind11.get_include())"
    OUTPUT_VARIABLE PYBIND11_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE PYBIND11_RESULT
)

if(NOT PYBIND11_RESULT EQUAL 0)
    message(FATAL_ERROR "pybind11 not found")
endif()

include_directories(
    ${Python3_INCLUDE_DIRS}
    ${PYBIND11_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}
)

add_executable(recommender 
    main.cpp 
    recommender.cpp 
    utils.cpp
)

target_link_libraries(recommender 
    ${Python3_LIBRARIES}
)

if(WIN32)
    target_link_libraries(recommender ws2_32)
endif()

set_target_properties(recommender PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
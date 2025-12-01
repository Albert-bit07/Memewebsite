 '''cmake
cmake_minimum_required(VERSION 3.15)
project(cpp_recommender)
set(CMAKE_CXX_STANDARD 17)

find_package(Python COMPONENTS Development REQUIRED)

#adjust these of pybind11 is installed system-wide
#We will use Python embedding without requiring pybind11 library, we will include pybind11 headers if available
#If you have pybind11 installed, set PYBIND11_DIR or use find_package(pybind11 REQUIRED)

include_directories(${Python_INCLUDE_DIRS})

add_executable(recommender main.cpp recommender.cpp utils.cpp)

target_link_libraries(recommender ${Python_LIBRARIES})

#On Linux you may need to add -lpython3.x; CMake's Python_LIBRARIES should
#IF building on macOS or Linux and link errors happen, point to the correct Python library.



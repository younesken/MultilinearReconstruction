cmake_minimum_required(VERSION 2.8.11)

project(MultilinearReconstruction)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

set(CMAKE_AUTOUIC ON)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

# Qt5
find_package(Qt5Core)
find_package(Qt5Widgets)
find_package(Qt5OpenGL)

# OpenGL
find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)
include_directories( ${OPENGL_INCLUDE_DIRS}  ${GLUT_INCLUDE_DIRS} )
link_libraries(${OPENGL_LIBRARIES} ${GLUT_LIBRARY})

find_package(GLEW REQUIRED)
if (GLEW_FOUND)
    include_directories(${GLEW_INCLUDE_DIRS})
    link_libraries(${GLEW_LIBRARIES})
endif()


# Eigen
find_package(Eigen REQUIRED)
include_directories(/usr/include/eigen3)

# MKL
include_directories(${MKL_INCLUDE_DIRS})
set(MKLROOT "/opt/intel/mkl")
set(MKLLIBS "-Wl,--start-group ${MKLROOT}/lib/intel64/libmkl_intel_lp64.a ${MKLROOT}/lib/intel64/libmkl_core.a ${MKLROOT}/lib/intel64/libmkl_intel_thread.a -Wl,--end-group -liomp5 -ldl -lpthread -lm")

# PhGLib
include_directories("$ENV{HOME}/SDKs/PhGLib/include")
set(PhGLib "-L$ENV{HOME}/SDKs/PhGLib/lib -lPhGLib")

# Ceres solver
find_package(Ceres REQUIRED)
include_directories(${CERES_INCLUDE_DIRS})
link_libraries(${CERES_LIBRARIES})


add_executable(test_mesh_visualizer test_mesh_visualizer.cpp)
target_link_libraries(test_mesh_visualizer basicmesh ioutilities multilinearmodel meshvisualizer
        Qt5::Core
        Qt5::Widgets
        Qt5::OpenGL
        ${MKLLIBS}
        ${PhGLib})

add_executable(test_mesh_visualizer2 test_mesh_visualizer2.cpp)
target_link_libraries(test_mesh_visualizer2 basicmesh ioutilities multilinearmodel meshvisualizer2
                Qt5::Core
                Qt5::Widgets
                Qt5::OpenGL
                ${MKLLIBS}
                ${PhGLib})

add_executable(test_offscreen_visualizer test_offscreen_visualizer.cpp)
target_link_libraries(test_offscreen_visualizer basicmesh ioutilities multilinearmodel offscreenmeshvisualizer
                Qt5::Core
                Qt5::Widgets
                Qt5::OpenGL
                ${MKLLIBS}
                ${PhGLib})


add_executable(test_tensors test_tensors.cpp)
target_link_libraries(test_tensors tensor)

add_executable(test_subdivision test_subdivision.cpp)
target_link_libraries(test_subdivision basicmesh)

add_executable(test_ceres test_ceres.cpp)
#target_link_libraries(test_ceres)

#set(MATLABROOT "/usr/local/MATLAB/R2016b/")
#set(MATLAB_LIBS "-L${MATLABROOT}/bin/glnxa64 -leng -lmat -lmex -lut -lmx")
#include_directories("${MATLABROOT}/extern/include")
#add_executable(test_matlab test_matlab.cpp)
#target_link_libraries(test_matlab ${MATLAB_LIBS})

add_executable(test_pipe test_pipe.cpp)

link_directories(..)

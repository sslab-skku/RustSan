cmake_minimum_required(VERSION 3.16.3)
project(AnalysisPass)


set(LLVM_BUILD "${PROJECT_SOURCE_DIR}/../../../llvm-project/build")
# set(LLVM_BUILD "${PROJECT_SOURCE_DIR}/llvm-project-12/build")
set(LLVM_DIR "${LLVM_BUILD}/lib/cmake/llvm")

find_package(LLVM REQUIRED CONFIG)
set(CMAKE_CXX_STANDARD 14)
# set(CMAKE_CXX_COMPILER "${LLVM_BUILD}/bin/clang++")

if(NOT LLVM_ENABLE_RTTI)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp")
add_definitions(${LLVM_DEFINITIONS})

# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(GLOB SRC ${SRC} Pass.cc)

if (EXISTS "${SVF_DIR}")
else()
    set(SVF_DIR $ENV{SVF_DIR})
    if(EXISTS "${SVF_DIR}")
    else()
    message(FATAL_ERROR "\
WARNING: The SVF_DIR var was not set (required for an out-of-source build)!\n\
Please set this to environment variable to point to the SVF_DIR directory or set this variable to cmake configuration\n
(e.g. on linux: export SVF_DIR=/path/to/SVF/dir) \n or \n \n(make the project via: cmake -DSVF_DIR=your_path_to_SVF) ")
    endif()
endif()

set (PDG_DIR "${SVF_DIR}/../program-dependence-graph/")
include_directories(${PDG_DIR}/include/)
# add_subdirectory(${PDG_DIR} build)
# set(PDG_LIB "${PDG_DIR}/build/libpdg_shared.a")
set(PDG_LIB "${PDG_DIR}/build/libpdg.so")
set(SVF_LIB "${SVF_DIR}/Release-build/lib/libSvf.a")


add_library(AnalysisPass SHARED ${SRC} ${SVF_LIB})
target_include_directories(AnalysisPass SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR} ${PDG_DIR}/include/ ${SVF_DIR}/include)


target_link_libraries(AnalysisPass PUBLIC ${SVF_LIB} ${llvm_libs})
# target_link_libraries(AnalysisPass PUBLIC ${PDG_LIB})

# target_link_directories(AnalysisPass PUBLIC ${PDG_DIR}/build)
# target_link_libraries(AnalysisPass PUBLIC pdg)

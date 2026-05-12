cmake_minimum_required(VERSION 3.20)

project(OrlixClangTidyModule LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED LLVM_DIR)
  set(LLVM_DIR "/opt/homebrew/opt/llvm/lib/cmake/llvm" CACHE PATH "LLVM CMake directory")
endif()
if(NOT DEFINED Clang_DIR)
  set(Clang_DIR "/opt/homebrew/opt/llvm/lib/cmake/clang" CACHE PATH "Clang CMake directory")
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

add_library(OrlixTidyModule MODULE
  OrlixAbstractionLeakageCheck.cpp
  OrlixAbiSurfaceCheck.cpp
  OrlixHostVocabularyCheck.cpp
  OrlixIncludeBoundaryCheck.cpp
  OrlixTypeOwnershipCheck.cpp
  OrlixTestVocabularyCheck.cpp
  OrlixTidyModule.cpp
  OrlixLoggingPolicyCheck.cpp
  OrlixSourcePolicyCheck.cpp
  OrlixTestPolicyCheck.cpp
)

target_include_directories(OrlixTidyModule PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(OrlixTidyModule PRIVATE
  ${LLVM_DEFINITIONS}
)

target_link_libraries(OrlixTidyModule PRIVATE
  clangTidy
  clangTidyUtils
  clangTooling
  clang-cpp
  LLVM
)

set_target_properties(OrlixTidyModule PROPERTIES
  PREFIX ""
  OUTPUT_NAME "OrlixTidyModule"
)

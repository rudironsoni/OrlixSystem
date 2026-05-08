cmake_minimum_required(VERSION 3.20)

project(IXLandClangTidyModule LANGUAGES C CXX)

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

add_library(IXLandTidyModule MODULE
  IXLandAbstractionLeakageCheck.cpp
  IXLandAbiSurfaceCheck.cpp
  IXLandHostVocabularyCheck.cpp
  IXLandIncludeBoundaryCheck.cpp
  IXLandTypeOwnershipCheck.cpp
  IXLandTestVocabularyCheck.cpp
  IXLandTidyModule.cpp
  IXLandLoggingPolicyCheck.cpp
  IXLandSourcePolicyCheck.cpp
  IXLandTestPolicyCheck.cpp
)

target_include_directories(IXLandTidyModule PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(IXLandTidyModule PRIVATE
  ${LLVM_DEFINITIONS}
)

target_link_libraries(IXLandTidyModule PRIVATE
  clangTidy
  clangTidyUtils
  clangBasic
  clangAST
  clangASTMatchers
  clangFrontend
  clangLex
  clangTooling
  clang-cpp
  LLVM
)

set_target_properties(IXLandTidyModule PROPERTIES
  PREFIX ""
  OUTPUT_NAME "IXLandTidyModule"
)

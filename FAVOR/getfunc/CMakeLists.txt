cmake_minimum_required(VERSION 3.4.3)
project(f1x)

set(LLVM_DIR "${LLVM}/share/llvm/cmake/")
set(Clang_DIR "${LLVM}/share/clang/cmake/")
set(F1X_CLANG_INCLUDE "${LLVM}/lib/clang/3.8.1/include/")

# LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Set your project compile flags.
# E.g. if using the C++ header files
# you will need to enable C++11 support
# for your compiler.

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Clang
find_package(Clang REQUIRED CONFIG)

llvm_map_components_to_libnames(llvm_libs option support)

add_executable(getfunc
  getfunc.cpp
  )

target_link_libraries(getfunc
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )

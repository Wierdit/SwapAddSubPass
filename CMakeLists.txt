cmake_minimum_required(VERSION 3.13.4)
project(SwapAddSubPass)

# Поиск LLVM
set(LLVM_DIR "/home/nastya/llvm_proj/llvm-project/build/lib/cmake/llvm/")

find_package(LLVM REQUIRED CONFIG)

# Отладочные сообщения
message(STATUS "Found LLVM ${LLVM_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM Library Dir: ${LLVM_LIBRARY_DIRS}")
message(STATUS "LLVM Include Dir: ${LLVM_INCLUDE_DIRS}")

# Настройка C++ стандарта
set(CMAKE_CXX_STANDARD ${LLVM_CXX_STD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "Set C++ Standard to ${CMAKE_CXX_STANDARD}")

# Добавление путей к заголовкам
include_directories(${LLVM_INCLUDE_DIRS})

# Определение библиотеки пасса
add_library(SwapAddSub MODULE SwapAddSubPass.cpp)

# Линковка с динамической библиотекой libLLVM.so
target_link_directories(SwapAddSub PRIVATE ${LLVM_LIBRARY_DIRS})
target_link_libraries(SwapAddSub PRIVATE LLVM)

cmake_minimum_required(VERSION 3.16)
project(cox C)

set(CMAKE_C_STANDARD 99)

add_executable(cox main.c common.h chunk.h chunk.c memory.h memory.c debug.c debug.h value.c value.h vm.c vm.h compiler.c compiler.h scanner.c scanner.h)
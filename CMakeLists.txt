cmake_minimum_required(VERSION 3.7)
project(server)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        config-parse.c
        config-parse.h
        main.c
        tcp-server.c http-parser.h http-parser.c.cpp dialog.h)

add_executable(server ${SOURCE_FILES})
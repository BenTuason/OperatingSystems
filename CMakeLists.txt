cmake_minimum_required(VERSION 3.27)
project(OS)

set(CMAKE_CXX_STANDARD 20)

include_directories(.)

add_executable(OS
        main.cpp
        main.h
        sample_matrix.txt
        sample_words.txt
        pa3.cpp)

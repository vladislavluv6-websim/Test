cmake_minimum_required(VERSION 3.22)
project(speedmod LANGUAGES CXX)

add_library(speedmod SHARED speedmod.cpp)

target_compile_features(speedmod PRIVATE cxx_std_20)
target_compile_options(speedmod PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden)

target_link_libraries(speedmod PRIVATE preloader log)
target_link_options(speedmod PRIVATE -Wl,-z,max-page-size=16384)

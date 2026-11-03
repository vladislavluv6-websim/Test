cmake_minimum_required(VERSION 3.22)
project(aimassist LANGUAGES CXX)

add_library(aimassist SHARED aimassist.cpp)

target_compile_features(aimassist PRIVATE cxx_std_20)
target_compile_options(aimassist PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden)

target_link_libraries(aimassist PRIVATE preloader log dl)
target_link_options(aimassist PRIVATE -Wl,-z,max-page-size=16384)

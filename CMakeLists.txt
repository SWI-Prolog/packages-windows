cmake_minimum_required(VERSION 2.8.12)
project(swipl-windows)

include("../cmake/PrologPackage.cmake")

if(WIN32)
swipl_plugin(
    windows
    MODULE plregtry
    C_SOURCES plregtry.c
    PL_LIBS registry.pl)
endif()

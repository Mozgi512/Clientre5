cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main esptool_py bootloader partition_table)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tab5_factory_updater)

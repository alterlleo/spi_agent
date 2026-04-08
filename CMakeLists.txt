#   ____  ____ ___      _                    _   
#  / ___||  _ \_ _|    / \   __ _  ___ _ __ | |_ 
#  \___ \| |_) | |    / _ \ / _` |/ _ \ '_ \| __|
#   ___) |  __/| |   / ___ \ (_| |  __/ | | | |_ 
#  |____/|_|  |___| /_/   \_\__, |\___|_| |_|\__|
#                           |___/                
#

cmake_minimum_required(VERSION 3.28)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(SPI_agent VERSION 1.0.0 LANGUAGES C CXX)
# set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)

# PROJECT SETTINGS #############################################################
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

if(UNIX AND NOT APPLE)
  set(LINUX TRUE)
  add_compile_options(-fPIC)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Get MADS prefix
execute_process(
  COMMAND mads -p
  OUTPUT_VARIABLE MADS_PREFIX_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
file(TO_CMAKE_PATH "${MADS_PREFIX_RAW}" MADS_PREFIX)
include_directories(${MADS_PREFIX}/include)
link_directories(${MADS_PREFIX}/lib)
set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE ${MADS_PREFIX})

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
  # No warning on FetchContent_Populate
  cmake_policy(SET CMP0169 OLD)
endif()

# option(MADS_INSTALL_AGENT "Install the FMU agent" OFF)
# option(MADS_BUILD_FMU "Build and instal the sample FMUs" OFF)

message(STATUS "MADS_PREFIX: ${MADS_PREFIX}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}") 
message(STATUS "mads-spi agent will be installed: ${MADS_INSTALL_AGENT}")

#   ____                            _                 _           
#  |  _ \  ___ _ __   ___ _ __   __| | ___ _ __   ___(_) ___  ___ 
#  | | | |/ _ \ '_ \ / _ \ '_ \ / _` |/ _ \ '_ \ / __| |/ _ \/ __|
#  | |_| |  __/ |_) |  __/ | | | (_| |  __/ | | | (__| |  __/\__ \
#  |____/ \___| .__/ \___|_| |_|\__,_|\___|_| |_|\___|_|\___||___/
#             |_|                                                 

include(FetchContent)
FetchContent_Declare(cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG        v3.1.1
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)


set(BUILD_TESTING OFF CACHE INTERNAL "")
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)


FetchContent_Declare(rang
  GIT_REPOSITORY https://github.com/pbosetti/rang.git
  GIT_TAG        HEAD
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)

# Disabilita le install() della dependency
set(_old_skip ${CMAKE_SKIP_INSTALL_RULES})
set(CMAKE_SKIP_INSTALL_RULES ON)
FetchContent_Populate(cxxopts)
add_subdirectory(${fmi4c_SOURCE_DIR} ${fmi4c_BINARY_DIR} EXCLUDE_FROM_ALL)
FetchContent_Populate(json)
add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
FetchContent_Populate(rang)
add_subdirectory(${rang_SOURCE_DIR} ${rang_BINARY_DIR} EXCLUDE_FROM_ALL)
set(CMAKE_SKIP_INSTALL_RULES ${_old_skip})

#   ____        _ _     _ 
#  | __ ) _   _(_) | __| |
#  |  _ \| | | | | |/ _` |
#  | |_) | |_| | | | (_| |
#  |____/ \__,_|_|_|\__,_|
#                       
#

# Agent is created in the src/main directory
file(GLOB LIB_SOURCES "${SRC_DIR}/*.c" "${SRC_DIR}/*.cpp")
add_subdirectory(${SRC_DIR}/main)
cmake_minimum_required(VERSION 2.8.12)
project(ctp7_modules)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Create a function to add modules
include(CMakeParseArguments)
function(add_module NAME)
  cmake_parse_arguments(ARGS "" "" "DEPS;LIBS" ${ARGN})
    
  add_library(${NAME} SHARED src/${NAME})
  target_link_libraries(${NAME} PRIVATE memhub ${ARGS_DEPS} ${ARGS_LIBS})

  set_target_properties(${NAME} PROPERTIES
    PREFIX ""
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/module_version_script.ld"
    LINK_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module_version_script.ld")


  install(TARGETS ${NAME} LIBRARY DESTINATION /mnt/persistent/rpcmodules)
  set_target_properties(${NAME} PROPERTIES INSTALL_RPATH /mnt/persistent/rpcmodules)
endfunction()

# A few definitions
add_compile_options(-std=c++11)
add_definitions(-DGEM_VARIANT=ge11)

# All headers are in include
include_directories("${CMAKE_SOURCE_DIR}/include")

# Add memhub
find_package(Libmemsvc REQUIRED)

add_library(memhub SHARED src/memhub.cpp)
target_link_libraries(memhub PUBLIC Libmemsvc::Libmemsvc)
install(TARGETS memhub
  LIBRARY DESTINATION /mnt/persistent/rpcmodules)

# Add the modules
add_module(memory)
add_module(utils
  LIBS xhalcore lmdb)
add_module(extras
  LIBS xhalcore lmdb)
add_module(amc
  DEPS utils extras
  LIBS xhalcore lmdb)
add_module(daq_monitor
  DEPS utils extras amc
  LIBS xhalcore lmdb)
add_module(vfat3
  DEPS utils extras amc
  LIBS xhalcore lmdb)
add_module(optohybrid
  DEPS utils extras amc
  LIBS xhalcore lmdb)
add_module(calibration_routines
  DEPS utils extras optohybrid vfat3 amc
  LIBS xhalcore lmdb)
add_module(gbt
  DEPS utils
  LIBS xhalcore lmdb)


cmake_minimum_required(VERSION 3.16)
set(CMAKE_ASC_ARCHITECTURES "dav-2201" CACHE STRING "NPU architecture")
find_package(ASC REQUIRED)
project(pearl_hlc2 LANGUAGES ASC CXX)
# Tuned config: NINNER=1 NFOLD=32 BUFS=4 VEC2X=2
set(NINNER "1"  CACHE STRING "inner matmuls per fold-step")
set(NFOLD  "32" CACHE STRING "fold-steps")
set(PEARL_PIPE_DEPTH "4" CACHE STRING "host strip queue depth")
set(BUFS "4" CACHE STRING "slot ping-pong depth")
set(BACKOFF "500" CACHE STRING "spin backoff iters")
set(VEC2X "2" CACHE STRING "vec consumers per cube producer")
add_library(pearl_hlc2 SHARED pearl_hlc2_cube.asc pearl_hlc2_vec.asc)
set_target_properties(pearl_hlc2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(pearl_hlc2 PRIVATE tiling_api register platform m dl pthread)
target_compile_options(pearl_hlc2 PRIVATE
    $<$<COMPILE_LANGUAGE:ASC>:--npu-arch=${CMAKE_ASC_ARCHITECTURES}>
    $<$<COMPILE_LANGUAGE:ASC>:-DNINNER=${NINNER}>
    $<$<COMPILE_LANGUAGE:ASC>:-DNFOLD=${NFOLD}>
    $<$<COMPILE_LANGUAGE:ASC>:-DPEARL_PIPE_DEPTH=${PEARL_PIPE_DEPTH}>
    $<$<COMPILE_LANGUAGE:ASC>:-DBUFS=${BUFS}>
    $<$<COMPILE_LANGUAGE:ASC>:-DBACKOFF=${BACKOFF}>
    $<$<COMPILE_LANGUAGE:ASC>:-DVEC2X=${VEC2X}>
)

# Experimental AICPU NEON kernel object, built with bisheng before ASC target link.
# AICPU linker compatibility is toolchain dependent; see EXPERIMENTAL.md.
set(AICPU_INC "$ENV{ASCEND_HOME_PATH}/asc/include/aicpu_api")
if(NOT EXISTS "${AICPU_INC}/aicpu_api.h")
  set(AICPU_INC "$ENV{ASCEND_HOME_PATH}/include/ascendc/aicpu_api")
endif()
set(AICPU_LIB "$ENV{ASCEND_HOME_PATH}/lib64/device/lib64")
if(NOT EXISTS "${AICPU_LIB}")
  set(AICPU_LIB "$ENV{ASCEND_HOME_PATH}/toolkit/lib64/device/lib64")
endif()
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pearl_aicpu_blake3.o
  COMMAND bisheng -O3 ${CMAKE_CURRENT_SOURCE_DIR}/pearl_aicpu_blake3.aicpu
  -I${AICPU_INC} --cce-aicpu-L${AICPU_LIB} --cce-aicpu-laicpu_api
  -c -o ${CMAKE_CURRENT_BINARY_DIR}/pearl_aicpu_blake3.o
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pearl_aicpu_blake3.aicpu VERBATIM)
add_custom_target(pearl_aicpu_obj DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/pearl_aicpu_blake3.o)
add_dependencies(pearl_hlc2 pearl_aicpu_obj)
target_link_libraries(pearl_hlc2 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/pearl_aicpu_blake3.o)

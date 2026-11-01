cmake_minimum_required(VERSION 3.16)
set(CMAKE_ASC_ARCHITECTURES "dav-2201" CACHE STRING "NPU architecture")
find_package(ASC REQUIRED)
find_package(AICPU REQUIRED)
project(pearl_hlc2 LANGUAGES ASC AICPU CXX)
# Tuned config: NINNER=1 NFOLD=32 BUFS=4 VEC2X=2
set(NINNER "1"  CACHE STRING "inner matmuls per fold-step")
set(NFOLD  "32" CACHE STRING "fold-steps")
set(PEARL_PIPE_DEPTH "4" CACHE STRING "host strip queue depth")
set(BUFS "4" CACHE STRING "slot ping-pong depth")
set(BACKOFF "500" CACHE STRING "spin backoff iters")
set(VEC2X "2" CACHE STRING "vec consumers per cube producer")
add_library(pearl_hlc2 SHARED pearl_hlc2_cube.asc pearl_hlc2_vec.asc pearl_aicpu_blake3.aicpu)
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

# Compile the AICPU kernel as a first-class language source in the SAME
# heterogeneous target. A separately generated .o linked as an ordinary host
# object can leave the host launch stub unresolved (pearl_aicpu_blake3(void*)).
# The AICPU CMake toolchain supplies the correct device packaging and host stub.
# Check undefined symbols immediately after linking the shared library.
find_program(PRL_NM_TOOL NAMES nm llvm-nm)
if(PRL_NM_TOOL)
  add_custom_command(TARGET pearl_hlc2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DNM=${PRL_NM_TOOL}
      -DLIB=$<TARGET_FILE:pearl_hlc2>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/check_aicpu_symbols.cmake
    VERBATIM)
endif()

# Fail the kernel build instead of deferring an unresolved host launch symbol
# to the final gcc miner link.
execute_process(COMMAND "${NM}" -D -C "${LIB}" OUTPUT_VARIABLE symbols ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(WARNING "Skipping AICPU symbol diagnostic: ${err}")
else()
  string(REPLACE "\n" ";" lines "${symbols}")
  foreach(line IN LISTS lines)
    if(line MATCHES "[ ]U[ ]+pearl_aicpu_blake3\\(void\\*\\)")
      message(FATAL_ERROR "Unresolved AICPU host launch stub in ${LIB}: ${line}")
    endif()
  endforeach()
endif()

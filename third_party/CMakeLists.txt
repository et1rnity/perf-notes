add_subdirectory(perf-cpp EXCLUDE_FROM_ALL)

include(CheckCXXSymbolExists)
check_cxx_symbol_exists(PERF_MEM_LVLNUM_UNC "linux/perf_event.h"
  PERF_NOTES_HAS_PERF_MEM_LVLNUM_UNC)
if(NOT PERF_NOTES_HAS_PERF_MEM_LVLNUM_UNC)
  # perf-cpp v1.1.1 uses this newer constant, absent from Debian 12 headers.
  # Linux UAPI: https://github.com/torvalds/linux/blob/v6.11/include/uapi/linux/perf_event.h
  target_compile_definitions(perf-cpp PRIVATE PERF_MEM_LVLNUM_UNC=0x08)
endif()

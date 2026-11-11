find_package(Threads REQUIRED)

add_executable(benchmark benchmark.cpp)

set(BATCH_SIZE_OFFSET "1" CACHE STRING
  "Batch release offset: 1..7 for a 128-slot ring")

if(NOT "${BATCH_SIZE_OFFSET}" MATCHES "^[1-7]$")
  message(FATAL_ERROR "BATCH_SIZE_OFFSET must be an integer from 1 to 7")
endif()

target_compile_definitions(benchmark PRIVATE
  BATCH_SIZE_OFFSET=${BATCH_SIZE_OFFSET}
)

include(CheckIPOSupported)
check_ipo_supported(LANGUAGES CXX)
set_property(TARGET benchmark PROPERTY
  INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

add_executable(c2c c2c.cpp)

if(PERF_EVENT)
  target_compile_definitions(benchmark PRIVATE PERF_EVENT)
  target_link_libraries(benchmark PRIVATE perf-cpp)
endif()

foreach(target IN ITEMS benchmark c2c)
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
  target_link_libraries(${target} PRIVATE Threads::Threads)
endforeach()

if(BUILD_TESTING)
  add_executable(ring_buffer_tests ring_buffer_tests.cpp)
  target_include_directories(ring_buffer_tests PRIVATE "${PROJECT_SOURCE_DIR}")
  target_link_libraries(ring_buffer_tests PRIVATE Threads::Threads)

  add_test(NAME ring_buffer.parse COMMAND ring_buffer_tests parse)
  foreach(strategy IN ITEMS classic piggyback batch final)
    foreach(scenario IN ITEMS empty single wraparound backpressure concurrent)
      add_test(NAME ring_buffer.${strategy}.${scenario}
        COMMAND ring_buffer_tests ${strategy} ${scenario})
      set_tests_properties(ring_buffer.${strategy}.${scenario} PROPERTIES
        TIMEOUT 20
        LABELS "unit;ring_buffer")
    endforeach()
  endforeach()
  set_tests_properties(ring_buffer.parse PROPERTIES
    TIMEOUT 20
    LABELS "unit;ring_buffer")
endif()

find_package(Threads REQUIRED)

add_executable(benchmark benchmark.cpp)
target_sources(benchmark PRIVATE
  new_ring_buffer.cpp
  classic_ring_buffer.cpp
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

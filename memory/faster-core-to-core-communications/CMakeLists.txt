find_package(Threads REQUIRED)

add_executable(benchmark benchmark.cpp)
add_executable(c2c c2c.cpp)

foreach(target IN ITEMS benchmark c2c)
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
  target_link_libraries(${target} PRIVATE Threads::Threads)
endforeach()

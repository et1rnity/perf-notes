find_package(Threads REQUIRED)

add_executable(tso-demo main.cpp)
target_compile_features(tso-demo PRIVATE cxx_std_20)
target_include_directories(tso-demo PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(tso-demo PRIVATE Threads::Threads)

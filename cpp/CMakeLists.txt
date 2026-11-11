find_package(Threads REQUIRED)

# Prefix targets with their relative path so examples named main.cpp do not
# collide with each other or with targets elsewhere in perf-notes.
add_custom_target(cpp_examples)

function(add_cpp_example source)
  string(REGEX REPLACE "\\.cpp$" "" target "${source}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" target "cpp_${target}")
  add_executable(${target} "${source}" ${ARGN})
  target_compile_features(${target} PRIVATE cxx_std_20)
  if(source MATCHES "^concurrency/")
    target_link_libraries(${target} PRIVATE Threads::Threads)
  endif()
  add_dependencies(cpp_examples ${target})
endfunction()

add_cpp_example(pimpl/main.cpp pimpl/widget.cpp)
add_cpp_example(sfinae/main.cpp)
add_cpp_example(lambda-expr/main.cpp)

foreach(source IN ITEMS
    language/concept.cpp
    language/crtp.cpp
    language/exception.cpp
    language/move_semantics.cpp
    language/virtual_layout.cpp
    language/volatile.cpp
    type-erasure/any_sink.cpp
    type-erasure/see_and_say.cpp
    template-meta-programming/expression_templates.cpp
    template-meta-programming/multi_layer_type_traits.cpp
    template-meta-programming/recursive_template_instantiation.cpp
    concurrency/01_thread.cpp
    concurrency/02_thread_bm.cpp
    concurrency/api/atomic.cpp
    concurrency/api/atomic_flag.cpp
    concurrency/api/cv.cpp
    concurrency/api/future.cpp
    concurrency/api/latch.cpp
    concurrency/api/lock.cpp
    concurrency/api/lock_deadlock.cpp
    concurrency/api/packaged_task.cpp
    concurrency/api/promise.cpp
    concurrency/api/unique_lock.cpp
    concurrency/coroutine/main.cpp
    concurrency/lock-free/naive_race.cpp
    concurrency/lock-free/spmc_fanout_example.cpp
    concurrency/lock-free/spmc_fanout_lossy.example.cpp
    concurrency/lock-free/spmc_lb_example.cpp
    concurrency/thread_pool/example.cpp)
  add_cpp_example("${source}")
endforeach()

# This example contains static_asserts and intentionally has no main().
add_library(cpp_template_meta_programming_type_list OBJECT
  template-meta-programming/type_list.cpp)
target_compile_features(cpp_template_meta_programming_type_list PRIVATE cxx_std_20)
add_dependencies(cpp_examples cpp_template_meta_programming_type_list)

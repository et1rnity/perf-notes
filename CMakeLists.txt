cmake_minimum_required(VERSION 3.16)

# Compiler selection must happen before project() enables C and C++.
if(NOT DEFINED CMAKE_C_COMPILER OR NOT DEFINED CMAKE_CXX_COMPILER)
  find_program(PERF_NOTES_CLANG_C NAMES clang)
  find_program(PERF_NOTES_CLANG_CXX NAMES clang++)

  if(NOT PERF_NOTES_CLANG_C OR NOT PERF_NOTES_CLANG_CXX)
    # find_program() does not expand wildcards. Scan PATH for versioned pairs.
    file(TO_CMAKE_PATH "$ENV{PATH}" clang_search_paths)
    set(clang_version 0)
    foreach(search_path IN LISTS clang_search_paths)
      file(GLOB clang_candidates LIST_DIRECTORIES false "${search_path}/clang++-*")
      foreach(candidate IN LISTS clang_candidates)
        get_filename_component(candidate_name "${candidate}" NAME)
        if(candidate_name MATCHES "^clang\\+\\+-([0-9]+)$")
          set(candidate_version "${CMAKE_MATCH_1}")
          set(c_compiler "${search_path}/clang-${candidate_version}")
          if(EXISTS "${c_compiler}" AND NOT IS_DIRECTORY "${c_compiler}"
            AND candidate_version VERSION_GREATER clang_version)
            set(clang_version "${candidate_version}")
            set(PERF_NOTES_CLANG_C "${c_compiler}")
            set(PERF_NOTES_CLANG_CXX "${candidate}")
          endif()
        endif()
      endforeach()
    endforeach()
  endif()

  if(NOT PERF_NOTES_CLANG_C OR NOT PERF_NOTES_CLANG_CXX)
    message(FATAL_ERROR
      "Clang pair not found in PATH (clang/clang++ or clang-N/clang++-N). "
      "Install Clang or set CMAKE_C_COMPILER and CMAKE_CXX_COMPILER.")
  endif()

  if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "${PERF_NOTES_CLANG_C}" CACHE FILEPATH "C compiler")
  endif()
  if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "${PERF_NOTES_CLANG_CXX}" CACHE FILEPATH "C++ compiler")
  endif()
endif()

project(perf-notes VERSION 0.1.0 LANGUAGES CXX C)

option(PERF_EVENT "Enable perf-cpp hardware event counting in benchmarks" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

if(PERF_EVENT)
  add_subdirectory(third_party)
endif()
add_subdirectory(memory/faster-core-to-core-communications)

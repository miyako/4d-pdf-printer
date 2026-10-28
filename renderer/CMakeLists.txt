cmake_minimum_required(VERSION 3.24)
project(pdf_render_worker VERSION 0.1.0 LANGUAGES CXX)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The initial Windows prototype supports x64 only")
endif()
if(WIN32 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64|X86_64")
  message(FATAL_ERROR "Windows ARM64 is not supported by this prototype")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GXPS REQUIRED IMPORTED_TARGET libgxps>=0.3.2)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo>=1.16)
pkg_check_modules(ARCHIVE REQUIRED IMPORTED_TARGET libarchive>=3.6)
pkg_get_variable(GXPS_FAIL_CLOSED libgxps worker_fail_closed)
if(NOT GXPS_FAIL_CLOSED STREQUAL "1")
  message(FATAL_ERROR "Use renderer/ports/libgxps overlay or tools/build-libgxps.py: stock 0.3.2 silently drops failed images")
endif()

add_executable(pdf-render-worker src/main.cpp src/output.cpp src/status.cpp)
target_compile_features(pdf-render-worker PRIVATE cxx_std_17)
target_link_libraries(pdf-render-worker PRIVATE PkgConfig::GXPS PkgConfig::CAIRO PkgConfig::ARCHIVE)
if(MSVC)
  target_compile_options(pdf-render-worker PRIVATE /W4 /WX /utf-8 /permissive-)
  target_compile_definitions(pdf-render-worker PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_options(pdf-render-worker PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  add_test(NAME worker-contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_worker.py"
      --worker "$<TARGET_FILE:pdf-render-worker>"
      --output-tests "$<TARGET_FILE:output-tests>"
      --schema "${CMAKE_CURRENT_SOURCE_DIR}/status-v1.schema.json")
  set_tests_properties(worker-contract PROPERTIES TIMEOUT 180)

  add_executable(output-tests tests/output_tests.cpp src/output.cpp src/status.cpp)
  target_compile_features(output-tests PRIVATE cxx_std_17)
  target_include_directories(output-tests PRIVATE src)
  target_link_libraries(output-tests PRIVATE PkgConfig::GXPS)
  if(MSVC)
    target_compile_definitions(output-tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
  add_test(NAME output-transactions COMMAND output-tests)
endif()

install(TARGETS pdf-render-worker RUNTIME DESTINATION bin)

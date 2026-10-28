cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0149)
  cmake_policy(SET CMP0149 OLD)
endif()
project(DesktopPdfPrototype LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "MSVC and Windows SDK 26100 are required.")
endif()
if(NOT CMAKE_SYSTEM_VERSION STREQUAL "10.0.26100.0" OR
   NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL "10.0.26100.0")
  message(FATAL_ERROR "Select SDK 10.0.26100.0 explicitly.")
endif()
if(NOT CMAKE_GENERATOR_PLATFORM MATCHES "^(x64|ARM64)$")
  message(FATAL_ERROR "Select x64 or ARM64 explicitly.")
endif()
if(NOT EXISTS "${DESKTOP_SDK_BIN}/midl.exe")
  message(FATAL_ERROR "Pass the explicit SDK 26100 host-tools directory.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" midl_arch)
set(rpc_dir "${CMAKE_CURRENT_BINARY_DIR}/rpc")
file(MAKE_DIRECTORY "${rpc_dir}")
add_custom_command(
  OUTPUT "${rpc_dir}/desktop-v1.h" "${rpc_dir}/desktop-v1_c.c" "${rpc_dir}/desktop-v1_s.c"
  COMMAND "${DESKTOP_SDK_BIN}/midl.exe" /nologo /robust /W4 /WX /env "${midl_arch}"
    /I "${DESKTOP_SDK_INCLUDE}/um" /I "${DESKTOP_SDK_INCLUDE}/shared"
    /acf "${CMAKE_CURRENT_SOURCE_DIR}/rpc/desktop-v1.acf"
    /out "${rpc_dir}" /h desktop-v1.h
    "${CMAKE_CURRENT_SOURCE_DIR}/rpc/desktop-v1.idl"
  DEPENDS rpc/desktop-v1.idl rpc/desktop-v1.acf
  VERBATIM)
add_custom_target(DesktopRpcGenerated DEPENDS
  "${rpc_dir}/desktop-v1.h" "${rpc_dir}/desktop-v1_c.c" "${rpc_dir}/desktop-v1_s.c")
add_library(DesktopRpcClient STATIC
  common/RpcClient.cpp rpc/Invoke.c "${rpc_dir}/desktop-v1_c.c")
add_dependencies(DesktopRpcClient DesktopRpcGenerated)
target_include_directories(DesktopRpcClient PUBLIC "${rpc_dir}")
add_executable(DesktopPdfCoordinator coordinator/Main.cpp "${rpc_dir}/desktop-v1_s.c")
add_dependencies(DesktopPdfCoordinator DesktopRpcGenerated)
target_include_directories(DesktopPdfCoordinator PRIVATE "${rpc_dir}")
target_link_libraries(DesktopPdfCoordinator PRIVATE windowsapp rpcrt4 advapi32 userenv)
add_executable(DesktopPdfCtl client/Main.cpp)
target_link_libraries(DesktopPdfCtl PRIVATE DesktopRpcClient gdi32 winspool ole32)
add_library(DesktopPdfTask SHARED psa/Task.cpp psa/Task.def)
target_link_libraries(DesktopPdfTask PRIVATE DesktopRpcClient)
add_executable(DesktopPdfApp WIN32 ../printer/psa/App.cpp)

foreach(target DesktopRpcClient DesktopPdfCoordinator DesktopPdfCtl DesktopPdfTask DesktopPdfApp)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN
    NOMINMAX _WIN32_WINNT=0x0A00)
  target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/EHsc;/permissive->")
endforeach()
target_link_libraries(DesktopRpcClient PUBLIC windowsapp rpcrt4 advapi32)
foreach(target DesktopPdfTask DesktopPdfApp)
  target_compile_definitions(${target} PRIVATE WINAPI_FAMILY=WINAPI_FAMILY_APP)
  target_link_options(${target} PRIVATE /APPCONTAINER)
  target_link_libraries(${target} PRIVATE windowsapp)
endforeach()
target_compile_definitions(DesktopRpcClient PRIVATE WINAPI_FAMILY=WINAPI_FAMILY_APP)
enable_testing()
add_test(NAME desktop-protocol-arguments COMMAND DesktopPdfCtl --self-test)
set_tests_properties(desktop-protocol-arguments PROPERTIES TIMEOUT 15)

cmake_minimum_required(VERSION 3.18)
project(pollen VERSION 0.1.4 DESCRIPTION "Pollen: a VRChat OSC app for Linux, Windows and macOS built with Fizmo, Grove and Walnut" LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(POLLEN_WITH_WALNUT "Build Walnut alongside Grove so Pollen's Walnut script works" ON)
option(POLLEN_BUILD_APP "Build the Grove app (turn off to build only the osc library and its tests)" ON)
option(POLLEN_BUILD_TESTS "Build the Pollen tests" ON)
option(POLLEN_PACKAGE "Make release packages: pollen with its window and compiled script built in, linked so it runs without a compiler" OFF)
option(POLLEN_BUNDLE "Run pollen-host --bundle as part of the build to make pollen with its window built in (turn off when cross-compiling, then bundle on the target)" ON)
set(POLLEN_FIZMO_TAG "v0.3.7" CACHE STRING "Fizmo git tag to fetch (needs the net component)")
set(POLLEN_GROVE_TAG "v0.2.8" CACHE STRING "Grove git tag to fetch")
set(POLLEN_WALNUT_TAG "v0.2.9" CACHE STRING "Walnut git tag Grove fetches")

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

include(FetchContent)
set(POLLEN_FETCH_EXTRA "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    list(APPEND POLLEN_FETCH_EXTRA SYSTEM)
endif()

FetchContent_Declare(fizmo
    GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Fizmo.git
    GIT_TAG        ${POLLEN_FIZMO_TAG}
    GIT_SHALLOW    TRUE
    ${POLLEN_FETCH_EXTRA})

if(POLLEN_BUILD_APP)
    set(GROVE_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
    set(GROVE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GROVE_WITH_WALNUT ${POLLEN_WITH_WALNUT} CACHE BOOL "" FORCE)
    set(GROVE_WALNUT_TAG ${POLLEN_WALNUT_TAG} CACHE STRING "" FORCE)
    set(GROVE_FIZMO_TAG ${POLLEN_FIZMO_TAG} CACHE STRING "" FORCE)
    FetchContent_Declare(grove
        GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Grove.git
        GIT_TAG        ${POLLEN_GROVE_TAG}
        GIT_SHALLOW    TRUE
        ${POLLEN_FETCH_EXTRA})
    FetchContent_MakeAvailable(fizmo grove)
else()
    FetchContent_MakeAvailable(fizmo)
endif()

if(TARGET fizmo::net)
    set(POLLEN_FIZMO_NET fizmo::net)
else()
    set(POLLEN_FIZMO_NET fizmo::fizmo)
endif()

function(pollen_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(POLLEN_BUILD_TESTS)
    enable_testing()
endif()

add_library(pollen_osc STATIC
    Source/osc/value.cpp
    Source/osc/message.cpp
    Source/osc/codec.cpp
    Source/osc/pattern.cpp
    Source/osc/dispatcher.cpp
    Source/osc/socket.cpp
    Source/osc/query/tree.cpp
    Source/osc/query/server.cpp
    Source/osc/query/mdns.cpp
    Source/osc/vrchat/vrchat.cpp
    Source/osc/vrchat/avatar.cpp
    Source/osc/vrchat/paths.cpp
    Source/osc/vrchat/query_link.cpp)
add_library(pollen::osc ALIAS pollen_osc)
target_compile_features(pollen_osc PUBLIC cxx_std_17)
target_include_directories(pollen_osc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include")
target_link_libraries(pollen_osc PUBLIC ${POLLEN_FIZMO_NET})
find_package(Threads REQUIRED)
target_link_libraries(pollen_osc PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(pollen_osc PUBLIC ws2_32)
endif()
pollen_warnings(pollen_osc)

add_library(pollen_core STATIC
    Source/pollen/settings.cpp
    Source/pollen/status.cpp
    Source/pollen/system_stats.cpp
    Source/pollen/media.cpp
    Source/pollen/media_windows.cpp
    Source/pollen/session.cpp
    Source/pollen/sensors.cpp
    Source/pollen/sensors_linux.cpp
    Source/pollen/sensors_unix.cpp
    Source/pollen/sensors_windows.cpp
    Source/pollen/nvml.cpp
    Source/pollen/modules.cpp
    Source/pollen/weather.cpp
    Source/pollen/vrchat_log.cpp
    Source/pollen/vrchat_api.cpp)
add_library(pollen::core ALIAS pollen_core)
target_include_directories(pollen_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Include")
target_link_libraries(pollen_core PUBLIC pollen_osc ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(pollen_core PUBLIC ole32 oleaut32)
endif()
pollen_warnings(pollen_core)

if(POLLEN_BUILD_TESTS)
    add_executable(pollen_tests Tests/pollen_tests.cpp)
    target_link_libraries(pollen_tests PRIVATE pollen_core)
    pollen_warnings(pollen_tests)
    add_test(NAME pollen_tests COMMAND pollen_tests)
endif()

if(NOT POLLEN_BUILD_APP)
    return()
endif()

set(POLLEN_BUNDLE_FLAGS "")
if(POLLEN_PACKAGE)
    set(POLLEN_BUNDLE_FLAGS --portable)
endif()

add_executable(pollen-host Tools/pollen.cpp)
target_link_libraries(pollen-host PRIVATE pollen_core grove::core grove::runtime)
target_compile_definitions(pollen-host PRIVATE
    "POLLEN_VERSION_STRING=\"${PROJECT_VERSION}\""
    "POLLEN_SOURCE_UI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/UI\"")
pollen_warnings(pollen-host)
if(WIN32)
    set_target_properties(pollen-host PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

add_custom_command(TARGET pollen-host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/UI" "$<TARGET_FILE_DIR:pollen-host>/ui")

file(GLOB_RECURSE POLLEN_UI_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/UI/*.grove" "${CMAKE_CURRENT_SOURCE_DIR}/UI/*.bark" "${CMAKE_CURRENT_SOURCE_DIR}/UI/*.wal")
set(POLLEN_BUNDLED "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/pollen${CMAKE_EXECUTABLE_SUFFIX}")
set(POLLEN_BUNDLE_DEPENDS pollen-host)
if(TARGET walnut)
    list(APPEND POLLEN_BUNDLE_DEPENDS walnut)
endif()
if(POLLEN_BUNDLE)
    add_custom_command(OUTPUT "${POLLEN_BUNDLED}"
        COMMAND ${CMAKE_COMMAND} -E env "POLLEN_UI_DIR=${CMAKE_CURRENT_SOURCE_DIR}/UI" $<TARGET_FILE:pollen-host> --bundle "${POLLEN_BUNDLED}" ${POLLEN_BUNDLE_FLAGS}
        DEPENDS ${POLLEN_BUNDLE_DEPENDS} ${POLLEN_UI_FILES}
        COMMENT "Building pollen with its window and compiled script built in"
        VERBATIM)
    add_custom_target(pollen ALL DEPENDS "${POLLEN_BUNDLED}")
endif()

if(POLLEN_PACKAGE)
    if(MSVC)
        message(FATAL_ERROR "POLLEN_PACKAGE needs a GCC or Clang toolchain, like Grove and Walnut")
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _pollen_cpu)
    if(_pollen_cpu MATCHES "^(x86_64|amd64|x64)$")
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(_pollen_arch i686)
        else()
            set(_pollen_arch x86_64)
        endif()
    elseif(_pollen_cpu MATCHES "^(aarch64|arm64)$")
        set(_pollen_arch arm64)
    elseif(_pollen_cpu MATCHES "^(i[3-6]86|x86)$")
        set(_pollen_arch i686)
    else()
        set(_pollen_arch ${_pollen_cpu})
    endif()

    if(MINGW)
        target_link_options(pollen-host PRIVATE -static)
        include(CheckCXXSourceCompiles)
        check_cxx_source_compiles("#include <cstdlib>\n#ifndef _UCRT\n#error msvcrt\n#endif\nint main() { return 0; }" POLLEN_MINGW_UCRT)
        if(_pollen_arch STREQUAL "arm64")
            set(_pollen_platform windows-clangarm64)
        elseif(_pollen_arch STREQUAL "i686")
            set(_pollen_platform windows-i686)
        elseif(POLLEN_MINGW_UCRT)
            set(_pollen_platform windows-ucrt64)
        else()
            set(_pollen_platform windows-mingw64)
        endif()
        set(CPACK_GENERATOR ZIP)
    elseif(APPLE)
        set(_pollen_platform macos-${_pollen_arch})
        set(CPACK_GENERATOR TGZ)
        set(_pollen_mac_libs "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib")
        foreach(t pollen-host)
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} "-DFILES=$<TARGET_FILE:${t}>" "-DLIB_DIR=${_pollen_mac_libs}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_bundle_libs.cmake"
                COMMENT "Copying the libraries ${t} needs into ${_pollen_mac_libs}"
                VERBATIM)
        endforeach()
        install(DIRECTORY "${_pollen_mac_libs}/" DESTINATION lib COMPONENT pollen USE_SOURCE_PERMISSIONS)
    else()
        target_link_options(pollen-host PRIVATE -static-libstdc++ -static-libgcc)
        set(_pollen_platform linux-${_pollen_arch})
        set(CPACK_GENERATOR TGZ)
    endif()
    set(POLLEN_PACKAGE_PLATFORM ${_pollen_platform} CACHE STRING "Platform name used in the package file name")

    if(POLLEN_BUNDLE)
        install(PROGRAMS "${POLLEN_BUNDLED}" DESTINATION . COMPONENT pollen)
    endif()
    install(FILES README.md DESTINATION . COMPONENT pollen)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
        install(FILES LICENSE DESTINATION . COMPONENT pollen)
    endif()
    set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};Pollen;pollen;/")

    set(CPACK_PACKAGE_NAME "Pollen")
    set(CPACK_PACKAGE_VENDOR "ThatNewCoderJSCpp")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "Pollen-${PROJECT_VERSION}-${POLLEN_PACKAGE_PLATFORM}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Pollen")
    include(CPack)
else()
    if(POLLEN_BUNDLE)
        install(PROGRAMS "${POLLEN_BUNDLED}" DESTINATION bin)
    endif()
endif()

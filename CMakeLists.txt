cmake_minimum_required(VERSION 3.27)

project(NovaCore
    VERSION 0.1.0
    DESCRIPTION "Ground-up C++23 Vulkan-first FPS Engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NOVACORE_ENABLE_SDL3 "Enable SDL3 platform window/input backend when available" ON)
option(NOVACORE_ENABLE_VULKAN "Enable Vulkan renderer backend when available" ON)
option(NOVACORE_BUILD_SERVER "Build the NovaCore dedicated server executable" ON)
option(NOVACORE_BUILD_TESTS "Build NovaCore smoke tests" ON)
option(NOVACORE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_library(novacore_engine
    engine/src/assets/AssetManifest.cpp
    engine/src/assets/AssetRegistry.cpp
    engine/src/assets/AssetStreamer.cpp
    engine/src/assets/AssetTypes.cpp
    engine/src/core/Application.cpp
    engine/src/core/Config.cpp
    engine/src/core/ConfigDocument.cpp
    engine/src/core/ConfigRegistry.cpp
    engine/src/core/FixedStep.cpp
    engine/src/core/Log.cpp
    engine/src/ecs/World.cpp
    engine/src/io/FileSystem.cpp
    engine/src/net/BitStream.cpp
    engine/src/net/Client.cpp
    engine/src/net/Loopback.cpp
    engine/src/net/Server.cpp
    engine/src/platform/InputAction.cpp
    engine/src/platform/Input.cpp
    engine/src/platform/Window.cpp
    engine/src/render/Renderer.cpp
)

target_include_directories(novacore_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(novacore_engine PUBLIC cxx_std_23)
add_library(Novacore::Engine ALIAS novacore_engine)
set_target_properties(novacore_engine PROPERTIES EXPORT_NAME Engine)

if(MSVC)
    target_compile_options(novacore_engine PRIVATE /W4 /permissive-)
    if(NOVACORE_WARNINGS_AS_ERRORS)
        target_compile_options(novacore_engine PRIVATE /WX)
    endif()
else()
    target_compile_options(novacore_engine PRIVATE -Wall -Wextra -Wpedantic)
    if(NOVACORE_WARNINGS_AS_ERRORS)
        target_compile_options(novacore_engine PRIVATE -Werror)
    endif()
endif()

set(NOVACORE_HAS_SDL3 0)
if(NOVACORE_ENABLE_SDL3)
    find_package(SDL3 CONFIG QUIET)
    if(SDL3_FOUND)
        set(NOVACORE_HAS_SDL3 1)
        target_link_libraries(novacore_engine PUBLIC SDL3::SDL3)
    else()
        message(WARNING "SDL3 was not found. Building with headless platform fallback.")
    endif()
endif()

set(NOVACORE_HAS_VULKAN 0)
if(NOVACORE_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        set(NOVACORE_HAS_VULKAN 1)
        target_link_libraries(novacore_engine PUBLIC Vulkan::Vulkan)
    else()
        message(WARNING "Vulkan SDK was not found. Building with null renderer fallback.")
    endif()
endif()

target_compile_definitions(novacore_engine
    PUBLIC
        NOVACORE_HAS_SDL3=${NOVACORE_HAS_SDL3}
        NOVACORE_HAS_VULKAN=${NOVACORE_HAS_VULKAN}
        NOVACORE_PROJECT_VERSION="${PROJECT_VERSION}"
)

if(NOVACORE_BUILD_SERVER)
    add_executable(novacore_server server/src/main.cpp)
    target_link_libraries(novacore_server PRIVATE novacore_engine)
endif()

enable_testing()

if(NOVACORE_BUILD_TESTS)
    add_executable(novacore_smoke_tests tests/novacore_smoke_tests.cpp)
    target_link_libraries(novacore_smoke_tests PRIVATE novacore_engine)
    add_test(NAME novacore_smoke_tests COMMAND novacore_smoke_tests)
endif()

include(CMakePackageConfigHelpers)

install(
    TARGETS novacore_engine
    EXPORT NovacoreTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(
    DIRECTORY engine/include/
    DESTINATION include
)

install(
    EXPORT NovacoreTargets
    NAMESPACE Novacore::
    DESTINATION lib/cmake/Novacore
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NovacoreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NovacoreConfig.cmake
    INSTALL_DESTINATION lib/cmake/Novacore
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/NovacoreConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/NovacoreConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/NovacoreConfigVersion.cmake
    DESTINATION lib/cmake/Novacore
)

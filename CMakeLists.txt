cmake_minimum_required(VERSION 3.27)

project(NovaCore
    VERSION 0.1.0
    DESCRIPTION "Ground-up C++23 Vulkan-first FPS Engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(NOVACORE_ENABLE_SDL3 "Enable SDL3 platform window/input backend when available" ON)
option(NOVACORE_FETCH_SDL3 "Fetch and build SDL3 when no installed SDL3 package is found" ON)
option(NOVACORE_ENABLE_VULKAN "Enable Vulkan renderer backend when available" ON)
option(NOVACORE_BUILD_SERVER "Build the NovaCore dedicated server executable" ON)
option(NOVACORE_BUILD_TESTS "Build NovaCore smoke tests" ON)
option(NOVACORE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
set(NOVACORE_SDL3_FETCH_TAG "release-3.2.22" CACHE STRING "SDL3 git tag used by the CMake FetchContent fallback")
set(NOVACORE_SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/novacore/shaders")

function(novacore_copy_mingw_runtime target_name)
    if(NOT MINGW)
        return()
    endif()

    get_filename_component(NOVACORE_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(runtime_dll IN ITEMS
        libatomic-1.dll
        libgcc_s_seh-1.dll
        libgomp-1.dll
        libssp-0.dll
        libstdc++-6.dll
        libwinpthread-1.dll
    )
        if(EXISTS "${NOVACORE_MINGW_BIN_DIR}/${runtime_dll}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${NOVACORE_MINGW_BIN_DIR}/${runtime_dll}"
                    "$<TARGET_FILE_DIR:${target_name}>/${runtime_dll}"
                VERBATIM
            )
        endif()
    endforeach()
endfunction()

add_library(novacore_engine
    engine/src/assets/GltfDocument.cpp
    engine/src/assets/GltfMetadata.cpp
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
    engine/src/physics/CharacterController.cpp
    engine/src/platform/InputAction.cpp
    engine/src/platform/Input.cpp
    engine/src/platform/Window.cpp
    engine/src/render/Mesh.cpp
    engine/src/render/Renderer.cpp
    engine/src/render/VulkanBackend.cpp
    engine/src/render/VulkanRuntime.cpp
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
set(NOVACORE_SDL3_TARGET "")
if(NOVACORE_ENABLE_SDL3)
    find_package(SDL3 CONFIG QUIET)

    if(NOT SDL3_FOUND AND NOVACORE_FETCH_SDL3)
        message(STATUS "SDL3 package was not found. Fetching SDL3 ${NOVACORE_SDL3_FETCH_TAG} for visible dev builds.")
        set(SDL_SHARED OFF CACHE BOOL "Build SDL3 shared library" FORCE)
        set(SDL_STATIC ON CACHE BOOL "Build SDL3 static library" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3 test library" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "Build SDL3 tests" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "Install SDL3" FORCE)
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG ${NOVACORE_SDL3_FETCH_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(SDL3)
    endif()

    foreach(candidate IN ITEMS SDL3::SDL3 SDL3::SDL3-static SDL3::SDL3-shared SDL3-static SDL3-shared)
        if(NOT NOVACORE_SDL3_TARGET AND TARGET "${candidate}")
            set(NOVACORE_SDL3_TARGET "${candidate}")
        endif()
    endforeach()

    if(NOVACORE_SDL3_TARGET)
        set(NOVACORE_HAS_SDL3 1)
        target_link_libraries(novacore_engine PUBLIC "$<BUILD_INTERFACE:${NOVACORE_SDL3_TARGET}>")
        message(STATUS "NovaCore SDL3 platform backend enabled via ${NOVACORE_SDL3_TARGET}.")
    else()
        message(WARNING "SDL3 was not found or fetched. Building with headless platform fallback.")
    endif()
endif()

set(NOVACORE_HAS_VULKAN 0)
if(NOVACORE_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        set(NOVACORE_HAS_VULKAN 1)
        target_link_libraries(novacore_engine PUBLIC Vulkan::Vulkan)

        if(Vulkan_GLSLC_EXECUTABLE)
            set(NOVACORE_SHADER_SPV_FILES "")
            foreach(shader_source IN ITEMS
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/debug_triangle.vert"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/debug_triangle.frag"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/world_box.vert"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/world_box.frag"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/world_line.vert"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/world_line.frag"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/world_mesh.vert"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/world_mesh.frag"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/ui_rect.vert"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/ui_line.vert"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/ui_flat.frag"
            )
                get_filename_component(shader_name "${shader_source}" NAME)
                set(shader_output "${NOVACORE_SHADER_BINARY_DIR}/${shader_name}.spv")
                add_custom_command(
                    OUTPUT "${shader_output}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOVACORE_SHADER_BINARY_DIR}"
                    COMMAND "${Vulkan_GLSLC_EXECUTABLE}" -O "${shader_source}" -o "${shader_output}"
                    DEPENDS "${shader_source}"
                    VERBATIM
                )
                list(APPEND NOVACORE_SHADER_SPV_FILES "${shader_output}")
            endforeach()

            add_custom_target(novacore_shaders DEPENDS ${NOVACORE_SHADER_SPV_FILES})
            add_dependencies(novacore_engine novacore_shaders)
            message(STATUS "NovaCore Vulkan shader compilation enabled via ${Vulkan_GLSLC_EXECUTABLE}.")
        else()
            message(WARNING "Vulkan SDK was found, but glslc was not found. Vulkan backend will run without compiled debug shaders.")
        endif()
    else()
        message(WARNING "Vulkan SDK was not found. Building with null renderer fallback.")
    endif()
endif()

target_compile_definitions(novacore_engine
    PUBLIC
        NOVACORE_HAS_SDL3=${NOVACORE_HAS_SDL3}
        NOVACORE_HAS_VULKAN=${NOVACORE_HAS_VULKAN}
        NOVACORE_PROJECT_VERSION="${PROJECT_VERSION}"
        NOVACORE_SHADER_BINARY_DIR="${NOVACORE_SHADER_BINARY_DIR}"
)

if(NOVACORE_BUILD_SERVER)
    add_executable(novacore_server server/src/main.cpp)
    target_link_libraries(novacore_server PRIVATE novacore_engine)
    novacore_copy_mingw_runtime(novacore_server)
endif()

enable_testing()

if(NOVACORE_BUILD_TESTS)
    add_executable(novacore_smoke_tests tests/novacore_smoke_tests.cpp)
    target_link_libraries(novacore_smoke_tests PRIVATE novacore_engine)
    novacore_copy_mingw_runtime(novacore_smoke_tests)
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

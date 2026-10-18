# CPU-only maintained rigid-body solver, intentionally opt-in during adoption.
option(NOVACORE_ENABLE_JOLT "Enable experimental fixed-tick Jolt rigid bodies" OFF)
set(NOVACORE_HAS_JOLT 0)
target_sources(novacore_engine PRIVATE engine/src/physics/RigidBodyWorld.cpp)

if(NOVACORE_ENABLE_JOLT)
    # Immutable v5.6.0 (2026-07-11), MIT. Existing source cache can be supplied
    # with FETCHCONTENT_SOURCE_DIR_JOLTPHYSICS; no SDK, samples or GUI required.
    function(novacore_add_jolt)
        set(OVERRIDE_CXX_FLAGS OFF)
        set(GENERATE_DEBUG_SYMBOLS OFF)
        set(INTERPROCEDURAL_OPTIMIZATION OFF)
        set(CROSS_PLATFORM_DETERMINISTIC ON)
        set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF)
        set(JPH_BUILD_SHARED_LIBS OFF)
        set(ENABLE_INSTALL ON)
        set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
        foreach(feature IN ITEMS ENABLE_ALL_WARNINGS ENABLE_OBJECT_STREAM
                FLOATING_POINT_EXCEPTIONS_ENABLED DEBUG_RENDERER_IN_DEBUG_AND_RELEASE
                DEBUG_RENDERER_IN_DISTRIBUTION PROFILER_IN_DEBUG_AND_RELEASE
                PROFILER_IN_DISTRIBUTION JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL
                JPH_USE_CPU_COMPUTE USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2
                USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
            set(${feature} OFF)
        endforeach()
        FetchContent_Declare(JoltPhysics
            GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
            GIT_TAG e77f175595e64cb44218cc9d9d56fc365ad0e36a
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR Build
        )
        FetchContent_MakeAvailable(JoltPhysics)
    endfunction()
    find_package(Jolt CONFIG QUIET)
    if(NOT TARGET Jolt::Jolt)
        novacore_add_jolt()
    else()
        message(STATUS "Using installed Jolt (adapter requires 5.6.0 and checks the runtime ABI).")
    endif()
    target_link_libraries(novacore_engine PRIVATE Jolt::Jolt)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../third_party/licenses/Jolt-MIT.txt"
        DESTINATION share/licenses/Jolt)
    set(NOVACORE_HAS_JOLT 1)
endif()

target_compile_definitions(novacore_engine PRIVATE NOVACORE_HAS_JOLT=${NOVACORE_HAS_JOLT})

if(NOVACORE_BUILD_TESTS)
    add_executable(novacore_rigid_body_tests tests/rigid_body_tests.cpp)
    target_link_libraries(novacore_rigid_body_tests PRIVATE novacore_engine)
    novacore_copy_mingw_runtime(novacore_rigid_body_tests)
    add_test(NAME novacore_rigid_body_tests COMMAND novacore_rigid_body_tests)
endif()

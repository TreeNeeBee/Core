# ==============================================================================
# Core Module - CMake Configuration
# ==============================================================================
# This CMakeLists.txt supports both:
# 1. Standalone build (with BuildTemplate as submodule)
# 2. Integrated build (as part of LightAP project)
# ==============================================================================

cmake_minimum_required(VERSION 3.10.2)

# ==============================================================================
# Detect Build Mode
# ==============================================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone mode: This is the root project
    set(CORE_STANDALONE_BUILD ON)
    message(STATUS "[Core] Standalone build mode detected")
    
    project(Core VERSION 1.0.0 LANGUAGES CXX)
    
    # C++ Standard Configuration (C++17 with C++14 fallback)
    if(NOT DEFINED CMAKE_CXX_STANDARD)
        set(CMAKE_CXX_STANDARD 17)
    endif()
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    
    # Detect C++17 compiler support
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-std=c++17" COMPILER_SUPPORTS_CXX17)
    if(COMPILER_SUPPORTS_CXX17)
        message(STATUS "[Core] C++17 support detected")
    else()
        set(CMAKE_CXX_STANDARD 14)
        message(STATUS "[Core] C++17 not available, falling back to C++14")
    endif()
    
    message(STATUS "[Core] Using C++ standard: ${CMAKE_CXX_STANDARD}")
    
    # Find dependencies
    set(CMAKE_THREAD_PREFER_PTHREAD OFF)
    if(APPLE)
        set(CMAKE_THREAD_LIBS_INIT "")
        set(CMAKE_USE_PTHREADS_INIT 1)
        set(Threads_FOUND TRUE)
    endif()
    find_package(Threads REQUIRED)
    find_package(Boost REQUIRED COMPONENTS filesystem regex system)
    find_package(GTest REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
    
    # Enable testing
    enable_testing()
    
    # Override BuildTemplate's RPATH defaults before including it
    # BuildTemplate sets CMAKE_BUILD_RPATH to "." which causes CWD-dependent library loading
    # RPATH search order: 1) current directory (.) 2) $ORIGIN for build, or install path
    set(CMAKE_BUILD_RPATH ".:$ORIGIN" CACHE STRING "Build RPATH" FORCE)
    set(CMAKE_INSTALL_RPATH "$ORIGIN" CACHE STRING "Install RPATH" FORCE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE CACHE BOOL "Don't add link directories to RPATH" FORCE)
    
    # Include BuildTemplate (should be in BuildTemplate/ subdirectory)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/BuildTemplate/Config.cmake.in")
        include("${CMAKE_CURRENT_SOURCE_DIR}/BuildTemplate/Config.cmake.in")
        set(BUILD_TEMPLATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/BuildTemplate")
        
        # Remove BuildTemplate's hardcoded -Wl,-rpath,. from compiler flags
        # This causes "." to be added to RUNPATH, making library loading CWD-dependent
        string(REPLACE "-Wl,-rpath,." "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
        string(REPLACE "-Wl,-rpath,." "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "C flags" FORCE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "CXX flags" FORCE)
    else()
        message(FATAL_ERROR 
            "[Core] BuildTemplate not found!\n"
            "Please initialize the submodule:\n"
            "  git submodule update --init --recursive\n"
            "Or clone with submodules:\n"
            "  git clone --recursive <repository-url>"
        )
    endif()
else()
    # Integrated mode: Part of LightAP project
    set(CORE_STANDALONE_BUILD OFF)
    message(STATUS "[Core] Integrated build mode (part of LightAP)")
    
    # Include BuildTemplate from parent project
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../BuildTemplate/Config.cmake.in")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../BuildTemplate/Config.cmake.in")
        set(BUILD_TEMPLATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../BuildTemplate")
    else()
        message(FATAL_ERROR "[Core] BuildTemplate not found at ../../BuildTemplate/")
    endif()
endif()

# ==============================================================================
# Module Configuration
# ==============================================================================
set(MODULE_NAME "Core")
set(MODULE_VERNO 1.0.0)
set(MODULE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(MODULE_SOURCE_CXX_DIR ${MODULE_ROOT_DIR}/source)
set(MODULE_EXTERNAL_LIB 
    Boost::filesystem 
    Boost::regex 
    ZLIB::ZLIB 
    OpenSSL::SSL 
    OpenSSL::Crypto
)

# ==============================================================================
# Build Shared Library
# ==============================================================================
# Create symlink for core headers to match include structure (lap/core)
if(NOT CORE_STANDALONE_BUILD)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/lap)
    file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/source/inc 
         ${CMAKE_CURRENT_BINARY_DIR}/include/lap/core SYMBOLIC)
    list(APPEND MODULE_EXTERNAL_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)
endif()

set(ENABLE_BUILD_SHARED_LIBRARY ON CACHE BOOL "Build core shared library" FORCE)
set(ENABLE_BUILD_WITH_PLATFORM_PREX ON)

include("${BUILD_TEMPLATE_DIR}/SharedLibrary.cmake.in")

# Configure library target
if(TARGET lap_core)
    # Ensure public symbols are exported for consumers (tests/examples)
    # BuildTemplate defaults to hidden visibility; override to default here for Core API
    set_target_properties(lap_core PROPERTIES
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
        # Set RPATH for runtime library search
        # BUILD: . (current dir first) then $ORIGIN (build dir relative)
        # INSTALL: $ORIGIN only (installed library location)
        BUILD_RPATH ".:$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
        # Don't skip RPATH for install
        SKIP_BUILD_RPATH FALSE
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )

    # Enable internal logging
    target_compile_definitions(lap_core PRIVATE __INNER_LOG_ENABLE)
    
    # Enforce strict memory layout checks
    target_compile_definitions(lap_core PRIVATE LAP_STRICT_LAYOUT=1)
    
    # Suppress packed-member address warning on GCC/Clang
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lap_core PRIVATE -Wno-address-of-packed-member)
    endif()
    
    # Optional: Require HMAC_SECRET environment variable (uncomment for strict mode)
    # target_compile_definitions(lap_core PRIVATE REQUIRE_HMAC_SECRET_ENV)
endif()

# ==============================================================================
# Unit Tests
# ==============================================================================
set(MODULE_TEST_DIR ${MODULE_ROOT_DIR}/test)
set(ENABLE_BUILD_TEST ON CACHE BOOL "Build core tests" FORCE)
set(ENABLE_BUILD_UNITTEST ON CACHE BOOL "Build core unit tests" FORCE)
set(MODULE_EXTERNAL_TEST_LIB 
    ${PLATFORM_SYSTEM_TARGET}_core 
    Threads::Threads 
    Boost::filesystem 
    Boost::regex 
    ZLIB::ZLIB 
    OpenSSL::SSL 
    OpenSSL::Crypto 
    GTest::GTest 
    GTest::Main
)

include("${BUILD_TEMPLATE_DIR}/Test.cmake.in")

# Configure test executable RPATH to find lap_core
# Tests/benchmarks are for development only - not installed
if(TARGET core_test)
    set_target_properties(core_test PROPERTIES
        # BUILD: . (current dir first) then $ORIGIN (build dir relative)
        BUILD_RPATH ".:$ORIGIN"
        # Tests are not installed, no INSTALL_RPATH needed
        SKIP_BUILD_RPATH FALSE
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# ==============================================================================
# Examples - Simple demonstration programs
# ==============================================================================
option(ENABLE_BUILD_EXAMPLES "Build core examples" ON)

if(ENABLE_BUILD_EXAMPLES)
    message(STATUS "[Core] Building examples...")
    
    # Helper function to create example targets
    function(add_core_example EXAMPLE_NAME SOURCE_FILE)
        add_executable(${EXAMPLE_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/test/examples/${SOURCE_FILE})
        lap_configure_cxx_target(TARGET ${EXAMPLE_NAME})
        target_include_directories(${EXAMPLE_NAME} PRIVATE ${MODULE_SOURCE_CXX_DIR}/inc)
        
        # Set RPATH: current dir first, then $ORIGIN (for development/debugging)
        set_target_properties(${EXAMPLE_NAME} PROPERTIES
            BUILD_RPATH ".:$ORIGIN"
            SKIP_BUILD_RPATH FALSE
        )
        
        # Parse optional additional libraries
        set(EXTRA_LIBS ${ARGN})
        target_link_libraries(${EXAMPLE_NAME} PRIVATE 
            ${PLATFORM_SYSTEM_TARGET}_core 
            Threads::Threads 
            ${EXTRA_LIBS}
        )
    endfunction()
    
    # Memory Management Examples
    add_core_example(core_memory_example memory_example.cpp Boost::filesystem Boost::regex)
    add_core_example(global_operator_test global_operator_test.cpp Boost::filesystem Boost::regex)
    add_core_example(direct_alignment_test direct_alignment_test.cpp 
        Boost::filesystem Boost::regex ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto)
    add_core_example(test_memory_allocator test_memory_allocator.cpp)
    add_core_example(test_memory_allocator_debug test_memory_allocator_debug.cpp)
    
    # Configuration Example (simplified)
    add_core_example(config_example config_example.cpp 
        Boost::filesystem Boost::regex ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto)
    
    # Core Classes Examples
    add_core_example(test_dynamic_magic test_dynamic_magic.cpp)
    add_core_example(test_core_classes test_core_classes.cpp)
    add_core_example(test_functional_classes test_functional_classes.cpp)
    
    # Memory Leak Detection Examples
    add_core_example(memory_leak_multithread_test memory_leak_multithread_test.cpp)
    add_core_example(memory_leak_intentional_test memory_leak_intentional_test.cpp)
    
    # AUTOSAR Examples
    add_core_example(abort_example abort_example.cpp)
    add_core_example(simple_init_test simple_init_test.cpp)
    add_core_example(initialization_example initialization_example.cpp)
endif()

# ==============================================================================
# Benchmarks - Performance and stress tests
# ==============================================================================
option(ENABLE_BUILD_BENCHMARKS "Build core benchmarks" ON)

if(ENABLE_BUILD_BENCHMARKS)
    message(STATUS "[Core] Building benchmarks...")
    
    # Helper function to create benchmark targets
    function(add_core_benchmark BENCHMARK_NAME SOURCE_FILE)
        add_executable(${BENCHMARK_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/test/benchmark/${SOURCE_FILE})
        lap_configure_cxx_target(TARGET ${BENCHMARK_NAME})
        target_include_directories(${BENCHMARK_NAME} PRIVATE ${MODULE_SOURCE_CXX_DIR}/inc)
        
        # Set RPATH: current dir first, then $ORIGIN (for development/debugging)
        set_target_properties(${BENCHMARK_NAME} PROPERTIES
            BUILD_RPATH ".:$ORIGIN"
            SKIP_BUILD_RPATH FALSE
        )
        
        # Parse optional additional libraries
        set(EXTRA_LIBS ${ARGN})
        target_link_libraries(${BENCHMARK_NAME} PRIVATE 
            ${PLATFORM_SYSTEM_TARGET}_core 
            Threads::Threads 
            ${EXTRA_LIBS}
        )
    endfunction()
    
    # Memory Performance Benchmarks
    add_core_benchmark(alignment_performance_test alignment_performance_test.cpp)
    add_core_benchmark(pool_vs_system_benchmark pool_vs_system_benchmark.cpp)
    add_core_benchmark(system_malloc_comparison_test system_malloc_comparison_test.cpp)
    
    # Stress Tests
    add_core_benchmark(memory_stress_test memory_stress_test.cpp Boost::filesystem Boost::regex)
endif()

# ==============================================================================
# Installation
# ==============================================================================
if(CORE_STANDALONE_BUILD)
    # Install library with export support
    install(TARGETS lap_core
        EXPORT CoreTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include/lap/core
    )
    
    # Install headers
    install(DIRECTORY ${MODULE_SOURCE_CXX_DIR}/inc/
        DESTINATION include/lap/core
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )
    
    # Install tools
    install(PROGRAMS tools/config_editor.py
        DESTINATION bin
        RENAME lap_config_editor
    )
    
    # Install documentation
    install(FILES 
        README.md
        CHANGES.md
        doc/QUICK_START.md
        doc/HMAC_SECRET_CONFIG.md
        DESTINATION share/doc/lap-core
    )
    
    # ===========================================================================
    # CMake Package Configuration (支持 find_package)
    # ===========================================================================
    include(CMakePackageConfigHelpers)
    
    # 导出目标
    install(EXPORT CoreTargets
        FILE CoreTargets.cmake
        NAMESPACE LightAP::
        DESTINATION lib/cmake/Core
        COMPONENT Development
    )
    
    # 配置包配置文件
    set(CMAKE_INSTALL_INCLUDEDIR "include/lap/core")
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/CoreConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/CoreConfig.cmake
        INSTALL_DESTINATION lib/cmake/Core
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR
    )
    
    # 生成版本文件
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/CoreConfigVersion.cmake
        VERSION ${MODULE_VERNO}
        COMPATIBILITY SameMajorVersion
    )
    
    # 安装配置文件
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/CoreConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/CoreConfigVersion.cmake
        DESTINATION lib/cmake/Core
        COMPONENT Development
    )
    
    message(STATUS "[Core] CMake package configuration will be installed to lib/cmake/Core")
endif()

# ==============================================================================
# Summary
# ==============================================================================
message(STATUS "==============================================================================")
message(STATUS "Core Module Configuration Summary")
message(STATUS "==============================================================================")
message(STATUS "  Build mode:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard:     C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Standalone:       ${CORE_STANDALONE_BUILD}")
message(STATUS "  Library:          ${PLATFORM_SYSTEM_TARGET}_core")
message(STATUS "  Unit tests:       ${ENABLE_BUILD_UNITTEST}")
message(STATUS "  Examples:         ${ENABLE_BUILD_EXAMPLES}")
message(STATUS "  Benchmarks:       ${ENABLE_BUILD_BENCHMARKS}")
if(NOT CORE_STANDALONE_BUILD)
    message(STATUS "  Install prefix:   ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "  Export targets:   YES (CoreTargets.cmake)")
endif()
message(STATUS "==============================================================================")

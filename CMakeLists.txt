# ==============================================================================
# Core Module - CMake Configuration (Simplified with BuildTemplate)
# ==============================================================================
# This CMakeLists.txt supports both:
# 1. Standalone build (with BuildTemplate as submodule)
# 2. Integrated build (as part of LightAP project)
# ==============================================================================

cmake_minimum_required(VERSION 3.10.2)

# ==============================================================================
# Detect Build Mode and Include BuildTemplate
# ==============================================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone mode
    set(CORE_STANDALONE_BUILD ON)
    message(STATUS "[Core] Standalone build mode detected")
    
    project(Core VERSION 1.0.0 LANGUAGES CXX)
    
    # Set default build type to Release
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (default: Release)" FORCE)
    endif()
    
    # C++ Standard Configuration
    if(NOT DEFINED CMAKE_CXX_STANDARD)
        set(CMAKE_CXX_STANDARD 17)
    endif()
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    
    # Detect C++17 support
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
    
    # Shared Memory IPC option
    option(ENABLE_SHARED_MEMORY_IPC "Enable shared memory allocator" OFF)
    
    enable_testing()
    
    # Override BuildTemplate's RPATH defaults
    set(CMAKE_BUILD_RPATH ".:$ORIGIN" CACHE STRING "Build RPATH" FORCE)
    set(CMAKE_INSTALL_RPATH "$ORIGIN" CACHE STRING "Install RPATH" FORCE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE CACHE BOOL "Don't add link dirs to RPATH" FORCE)
    
    # Include BuildTemplate
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/BuildTemplate/Config.cmake.in")
        include("${CMAKE_CURRENT_SOURCE_DIR}/BuildTemplate/Config.cmake.in")
        include("${CMAKE_CURRENT_SOURCE_DIR}/BuildTemplate/IPCHelpers.cmake.in")
        include("${CMAKE_CURRENT_SOURCE_DIR}/BuildTemplate/ModuleHelpers.cmake.in")
        set(BUILD_TEMPLATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/BuildTemplate")
        
        # Remove BuildTemplate's hardcoded -Wl,-rpath,.
        string(REPLACE "-Wl,-rpath,." "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
        string(REPLACE "-Wl,-rpath,." "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "C flags" FORCE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "CXX flags" FORCE)
    else()
        message(FATAL_ERROR 
            "[Core] BuildTemplate not found! Run: git submodule update --init --recursive")
    endif()
else()
    # Integrated mode
    set(CORE_STANDALONE_BUILD OFF)
    message(STATUS "[Core] Integrated build mode (part of LightAP)")
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../BuildTemplate/Config.cmake.in")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../BuildTemplate/Config.cmake.in")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../BuildTemplate/IPCHelpers.cmake.in")
        include("${CMAKE_CURRENT_SOURCE_DIR}/../../BuildTemplate/ModuleHelpers.cmake.in")
        set(BUILD_TEMPLATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../BuildTemplate")
    else()
        message(FATAL_ERROR "[Core] BuildTemplate not found at ../../BuildTemplate/")
    endif()
endif()

# ==============================================================================
# Module Configuration
# ==============================================================================
# Note: lap_setup_module from ModuleHelpers expects TARGET parameter
# We'll configure manually here since we have complex setup
message(STATUS "[Core] Configuring module...")

# Create symlink for core headers (lap/core structure)
if(NOT CORE_STANDALONE_BUILD)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/lap)
    file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/source/inc 
         ${CMAKE_CURRENT_BINARY_DIR}/include/lap/core SYMBOLIC)
    list(APPEND MODULE_EXTERNAL_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)
endif()

# Print configuration summary
lap_print_config()

# ==============================================================================
# Build Main Shared Library (NORMAL mode by default)
# ==============================================================================
set(MODULE_NAME "core")
set(MODULE_VERSION "1.0.0")
set(MODULE_VERNO "1.0.0")
set(MODULE_SOURCE_CXX_DIR "source")
set(MODULE_INCLUDE_DIR "source/inc")
set(MODULE_EXTERNAL_LIB 
    Boost::filesystem 
    Boost::regex 
    Boost::system
    ZLIB::ZLIB 
    OpenSSL::SSL 
    OpenSSL::Crypto
    Threads::Threads
)
set(ENABLE_BUILD_SHARED_LIBRARY ON CACHE BOOL "Build core shared library" FORCE)
set(ENABLE_BUILD_WITH_PLATFORM_PREX ON)

include("${BUILD_TEMPLATE_DIR}/SharedLibrary.cmake.in")

# Configure library target
if(TARGET lap_core)
    set_target_properties(lap_core PROPERTIES
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
        BUILD_RPATH ".:$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
        SKIP_BUILD_RPATH FALSE
        BUILD_WITH_INSTALL_RPATH FALSE
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
    
    target_compile_definitions(lap_core PRIVATE 
        __INNER_LOG_ENABLE
        LAP_STRICT_LAYOUT=1
    )
    
    # IPC Memory Footprint Configuration
    set(LIGHTAP_IPC_MODE "NORMAL" CACHE STRING "IPC mode: SHRINK|NORMAL|EXTEND")
    set_property(CACHE LIGHTAP_IPC_MODE PROPERTY STRINGS "SHRINK" "NORMAL" "EXTEND")
    lap_configure_ipc_mode(TARGET lap_core MODE ${LIGHTAP_IPC_MODE})
    
    # IPC shared memory option
    if(ENABLE_SHARED_MEMORY_IPC)
        target_compile_definitions(lap_core PUBLIC ENABLE_SHARED_MEMORY_IPC)
    endif()
    
    # Suppress packed-member address warning
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lap_core PRIVATE -Wno-address-of-packed-member)
    endif()
endif()

# ==============================================================================
# Unit Tests
# ==============================================================================
option(ENABLE_BUILD_UNITTEST "Build unit tests" ON)

if(ENABLE_BUILD_UNITTEST AND TARGET lap_core)
    message(STATUS "[Core] Building unit tests...")
    
    lap_add_ipc_test(
        NAME test_controlblock_size
        SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test/ipc/test_controlblock_size.cpp
        LINK_LIBRARIES lap_core Threads::Threads rt
        INCLUDE_DIRS ${MODULE_SOURCE_CXX_DIR}/inc
    )

    lap_add_ipc_test(
        NAME test_ipc_publisher
        SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test/unittest/test_ipc_publisher.cpp
        LINK_LIBRARIES lap_core Threads::Threads rt
        INCLUDE_DIRS ${MODULE_SOURCE_CXX_DIR}/inc
    )

    lap_add_ipc_test(
        NAME test_ipc_subscriber
        SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test/unittest/test_ipc_subscriber.cpp
        LINK_LIBRARIES lap_core Threads::Threads rt
        INCLUDE_DIRS ${MODULE_SOURCE_CXX_DIR}/inc
    )

    lap_add_ipc_test(
        NAME test_ipc_factory
        SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test/unittest/test_ipc_factory.cpp
        LINK_LIBRARIES lap_core Threads::Threads rt
        INCLUDE_DIRS ${MODULE_SOURCE_CXX_DIR}/inc
    )
endif()

# ==============================================================================
# Examples
# ==============================================================================
option(ENABLE_BUILD_EXAMPLES "Build examples" ON)

if(ENABLE_BUILD_EXAMPLES AND TARGET lap_core)
    message(STATUS "[Core] Building examples...")
    
    lap_add_example_executable(
        NAME ipc_chain_example
        SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test/ipc/ipc_chain_example.cpp
        LINK_LIBRARIES lap_core Threads::Threads rt
        INCLUDE_DIRS ${MODULE_SOURCE_CXX_DIR}/inc
        ENABLE_LOGGING
    )
    
    # NORMAL mode examples (batch processing)
    lap_add_examples_batch(
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/ipc
        SOURCES camera_fusion_spmc_example.cpp camera_fusion_mpsc_example.cpp camera_fusion_spsc_example.cpp camera_fusion_mpmc_example.cpp ipc_service_example.cpp property_board_example.cpp
        LINK_LIBRARIES lap_core Threads::Threads rt
        INCLUDE_DIRS ${MODULE_SOURCE_CXX_DIR}/inc
        ENABLE_LOGGING
    )
    
    lap_add_examples_batch(
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/examples
        SOURCES config_example.cpp
        LINK_LIBRARIES lap_core ${MODULE_EXTERNAL_LIB}
        INCLUDE_DIRS ${MODULE_SOURCE_CXX_DIR}/inc
    )
endif()

# ==============================================================================
# Benchmarks
# ==============================================================================
option(ENABLE_BUILD_BENCHMARKS "Build benchmarks" ON)

if(ENABLE_BUILD_BENCHMARKS AND TARGET lap_core)
    message(STATUS "[Core] Building benchmarks...")
    
    # Add benchmark examples here using lap_add_benchmark_executable()
    # (Currently no benchmarks defined)
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
    
    # CMake Package Configuration
    include(CMakePackageConfigHelpers)
    
    install(EXPORT CoreTargets
        FILE CoreTargets.cmake
        NAMESPACE LightAP::
        DESTINATION lib/cmake/Core
        COMPONENT Development
    )
    
    set(CMAKE_INSTALL_INCLUDEDIR "include/lap/core")
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/CoreConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/CoreConfig.cmake
        INSTALL_DESTINATION lib/cmake/Core
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR
    )
    
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/CoreConfigVersion.cmake
        VERSION ${MODULE_VERNO}
        COMPATIBILITY SameMajorVersion
    )
    
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/CoreConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/CoreConfigVersion.cmake
        DESTINATION lib/cmake/Core
        COMPONENT Development
    )
    
    message(STATUS "[Core] CMake package will be installed to lib/cmake/Core")
endif()

# ==============================================================================
# Summary
# ==============================================================================
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(BUILD_MODE_DISPLAY "Debug (default)")
else()
    set(BUILD_MODE_DISPLAY "${CMAKE_BUILD_TYPE}")
endif()

message(STATUS "==============================================================================")
message(STATUS "Core Module Configuration Summary")
message(STATUS "==============================================================================")
message(STATUS "  Build mode:       ${BUILD_MODE_DISPLAY}")
message(STATUS "  C++ standard:     C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Standalone:       ${CORE_STANDALONE_BUILD}")
message(STATUS "  Library:          lap_core")
message(STATUS "  Unit tests:       ${ENABLE_BUILD_UNITTEST}")
message(STATUS "  Examples:         ${ENABLE_BUILD_EXAMPLES}")
message(STATUS "  Benchmarks:       ${ENABLE_BUILD_BENCHMARKS}")
if(CORE_STANDALONE_BUILD)
    message(STATUS "  Install prefix:   ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "  Export targets:   YES (CoreTargets.cmake)")
endif()
message(STATUS "==============================================================================")

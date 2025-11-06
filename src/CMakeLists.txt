cmake_minimum_required(VERSION 3.0)
project(xpmclient)

include(TestBigEndian)
test_big_endian(IS_BIGENDIAN)

configure_file(
  ${CMAKE_SOURCE_DIR}/xpm/rpc/include/config.h.in
  ${CMAKE_BINARY_DIR}/config.h
)

include_directories(${CMAKE_BINARY_DIR})

set (CMAKE_CXX_STANDARD 17)
option(STATIC_BUILD "Build with static libraries on Linux")
option(SANITIZER_ENABLED "Build with address sanitizer" OFF)
option(GPROF_ENABLED "Build with GNU profiler (use gprof ./exename -p > out.txt)" OFF)
option(EQUIHASH_ENABLED "Build equihash OpenCL GPU client" OFF)

if (SANITIZER_ENABLED)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()

if (GPROF_ENABLED)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
endif()

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  ${CMAKE_SOURCE_DIR}/cmake
)

include(FindDependencies)
# Force use of system-installed protobuf
find_package(Protobuf REQUIRED)

if (Protobuf_FOUND)
  message(STATUS "Using system Protobuf version: ${Protobuf_VERSION}")
  set(Protobuf_INCLUDE_DIR /usr/include)
  set(Protobuf_LIBRARIES /usr/lib/x86_64-linux-gnu/libprotobuf.so)
  set(Protobuf_PROTOC_EXECUTABLE /usr/bin/protoc)
endif()

# OpenCL optional detection (skip building OpenCL client if not available)
find_package(OpenCL QUIET)

# CLRX detection (auto-detect with manual override support)
if (OpenCL_FOUND)
  set(CLRX_ROOT "$ENV{HOME}/CLRX-mirror")

  # Headers
  if (NOT CLRX_INCLUDE_DIRECTORY)
    find_path(CLRX_INCLUDE_DIRECTORY
      CLRX/amdbin/AmdBinaries.h
      HINTS
        ${CLRX_ROOT}
        ${CLRX_ROOT}/include
    )
  endif()

  if (NOT CLRX_CONFIG_INCLUDE_DIRECTORY)
    find_path(CLRX_CONFIG_INCLUDE_DIRECTORY
      CLRX/Config.h
      HINTS
        ${CLRX_ROOT}
        ${CLRX_ROOT}/include
        ${CLRX_ROOT}/build
    )
  endif()

  # Libraries
  if (NOT CLRX_AMDASM_LIBRARY)
    find_library(CLRX_AMDASM_LIBRARY
      NAMES CLRXAmdAsm
      HINTS
        ${CLRX_ROOT}/build/amdasm
        ${CLRX_ROOT}/build/lib
    )
  endif()

  if (NOT CLRX_AMDBIN_LIBRARY)
    find_library(CLRX_AMDBIN_LIBRARY
      NAMES CLRXAmdBin
      HINTS
        ${CLRX_ROOT}/build/amdbin
        ${CLRX_ROOT}/build/lib
    )
  endif()

  if (NOT CLRX_UTILS_LIBRARY)
    find_library(CLRX_UTILS_LIBRARY
      NAMES CLRXUtils
      HINTS
        ${CLRX_ROOT}/build/utils
        ${CLRX_ROOT}/build/lib
    )
  endif()

  # Flag check
  set(CLRX_ALL_FOUND FALSE)
  if (CLRX_INCLUDE_DIRECTORY AND CLRX_AMDASM_LIBRARY AND CLRX_AMDBIN_LIBRARY AND CLRX_UTILS_LIBRARY)
    set(CLRX_ALL_FOUND TRUE)
  endif()

  # Optional debug output
  message(STATUS "CLRX_INCLUDE_DIRECTORY: ${CLRX_INCLUDE_DIRECTORY}")
  message(STATUS "CLRX_AMDASM_LIBRARY: ${CLRX_AMDASM_LIBRARY}")
  message(STATUS "CLRX_AMDBIN_LIBRARY: ${CLRX_AMDBIN_LIBRARY}")
  message(STATUS "CLRX_UTILS_LIBRARY: ${CLRX_UTILS_LIBRARY}")
endif()

# CUDA detection: prefer CUDAToolkit (>= 3.17), otherwise fallback to legacy FindCUDA
if (NOT CMAKE_CROSSCOMPILING)
  if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.17")
    find_package(CUDAToolkit QUIET)
    if (CUDAToolkit_FOUND)
      set(CUDA_FOUND TRUE)
      set(CUDA_INCLUDE_DIRS ${CUDAToolkit_INCLUDE_DIRS})
      message(STATUS "CUDA Toolkit found: ${CUDAToolkit_VERSION}")
      message(STATUS "CUDA Include: ${CUDAToolkit_INCLUDE_DIRS}")
    else()
      # Only attempt legacy FindCUDA when CMake < 3.27 to avoid removed-module errors
      if (CMAKE_VERSION VERSION_LESS "3.27")
        find_package(CUDA QUIET)
        if (CUDA_FOUND)
          message(STATUS "CUDA found (legacy FindCUDA)")
        endif()
      endif()
    endif()
  else()
    find_package(CUDA QUIET)
    if (CUDA_FOUND)
      message(STATUS "CUDA found (legacy FindCUDA)")
    endif()
  endif()
else()
  find_path(CUDA_TOOLKIT_INCLUDE
    device_functions.h
    PATHS ${CUDA_TOOLKIT_ROOT_DIR}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
  )
  if (NOT CUDA_TOOLKIT_INCLUDE STREQUAL "CUDA_TOOLKIT_INCLUDE-NOTFOUND")
    set(CUDA_FOUND 1)
  else()
    message("CUDA package not found")
  endif()
endif()

include_directories(
  ${GMP_INCLUDE_DIRECTORY}
  ${ZMQ_INCLUDE_DIRECTORY}
  ${PROTOBUF_INCLUDE_DIRECTORY}
  ${OpenCL_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
  ${CMAKE_SOURCE_DIR}/config4cpp/include
  ${CMAKE_BINARY_DIR}
)

if (CMAKE_COMPILER_IS_GNUCC)
  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 6.0)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated -Wno-ignored-attributes")
  endif()
endif()

if (APPLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCL_SILENCE_DEPRECATION")
endif()

# If OpenCL is available, set target OpenCL version to reduce default 3.0 header warnings
if (OpenCL_FOUND)
  add_definitions(-DCL_TARGET_OPENCL_VERSION=200)
endif()

if (WIN32)
  message("Building for Win32")
  add_definitions(-D_WIN32 -D__WINDOWS__ -D__USE_MINGW_ANSI_STDIO=0)
else()
  message("Building for Linux")
  add_definitions(-DLINUX)
  if (STATIC_BUILD)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
  endif()
endif()

file(GLOB config4cppFiles "${CMAKE_SOURCE_DIR}/config4cpp/src/*.cpp")
add_library(config4cpp ${config4cppFiles})

PROTOBUF_GENERATE_CPP(ProtoSources ProtoHeaders ${CMAKE_SOURCE_DIR}/protocol.proto)

set(LIBRARIES
  ${ZMQ_LIBRARY}
  ${SODIUM_LIBRARY}
  ${GMP_LIBRARY}
  ${PROTOBUF_LIBRARY}
  config4cpp
)

if (WIN32)
  set(LIBRARIES ${LIBRARIES} ws2_32 advapi32)
else()
  set(LIBRARIES ${LIBRARIES} dl)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

# XPM GPU Client
if (WIN32)
  set(AMD_MONITORING_SOURCES
    hwmon-adl.cpp
  )
else()
  set(AMD_MONITORING_SOURCES
    hwmon-adl.cpp
    hwmon-amdgpu.cpp
  )
endif()

file(GLOB BLKMAKER_SOURCES
  ${CMAKE_SOURCE_DIR}/xpm/rpc/blkmaker/*.c
)
add_library(blkmaker STATIC ${BLKMAKER_SOURCES})

target_include_directories(blkmaker PUBLIC
  ${CMAKE_SOURCE_DIR}/xpm/rpc/blkmaker
)

if (OpenCL_FOUND AND CLRX_ALL_FOUND)
  add_executable(xpmclient
    adl.cpp
    baseclient.cpp
    ${AMD_MONITORING_SOURCES}
    loguru.cpp
    opencl.cpp
    prime.cpp
    sha256.cpp
    zmqextras.cpp
    xpm/opencl/xpmclient.cpp
    xpm/opencl/benchmarks.cpp
    codegen/generic.cpp
    codegen/gcn.cpp
    xpm/rpc/common/getblocktemplate.cpp
    xpm/rpc/common/primecoin.cpp
    xpm/rpc/common/system.cpp
    ${ProtoSources}
  )

  target_include_directories(xpmclient PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/xpm/opencl
    ${CMAKE_SOURCE_DIR}/xpm/rpc/blkmaker
    ${CMAKE_SOURCE_DIR}/xpm/rpc/common
    ${CMAKE_SOURCE_DIR}/xpm/rpc/include
    ${CLRX_INCLUDE_DIRECTORY}
    ${CLRX_CONFIG_INCLUDE_DIRECTORY}
  )

  target_link_libraries(xpmclient
    ${LIBRARIES}
    ${OpenCL_LIBRARIES}
    ${CLRX_AMDASM_LIBRARY}
    ${CLRX_AMDBIN_LIBRARY}
    ${CLRX_UTILS_LIBRARY}
    blkmaker
    curl
    jansson
    crypto
    ssl
    stdc++
  )

  if (STATIC_BUILD)
    set_target_properties(xpmclient PROPERTIES LINK_SEARCH_END_STATIC 1)
  endif()

  # Post-build: copy OpenCL kernels and config
  file(GLOB OPENCL_GENERIC_KERNELS "${CMAKE_SOURCE_DIR}/xpm/opencl/generic_*")
  file(GLOB OPENCL_GCN_KERNELS "${CMAKE_SOURCE_DIR}/xpm/opencl/gcn_*")
  add_custom_command(TARGET xpmclient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/xpm/opencl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${OPENCL_GENERIC_KERNELS}
      ${OPENCL_GCN_KERNELS}
      ${CMAKE_BINARY_DIR}/xpm/opencl/
    COMMENT "Copying OpenCL kernel source files to build directory"
  )

# Copy OpenCL config.txt to build directory (binary-safe)
if (EXISTS ${CMAKE_SOURCE_DIR}/xpm/opencl/config.txt)
file(COPY
    ${CMAKE_SOURCE_DIR}/xpm/opencl/config.txt
    DESTINATION ${CMAKE_BINARY_DIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)
endif()
else()
  if (NOT OpenCL_FOUND)
    message(STATUS "OpenCL not found. Skipping OpenCL client (xpmclient).\n  Install hint: sudo apt install ocl-icd-opencl-dev")
  elseif (NOT CLRX_ALL_FOUND)
    message(STATUS "CLRX not found (headers or libraries missing). Skipping OpenCL client (xpmclient).\n  Required: -DCLRX_INCLUDE_DIRECTORY -DCLRX_AMDASM_LIBRARY -DCLRX_AMDBIN_LIBRARY -DCLRX_UTILS_LIBRARY\n  Typical install: git clone https://github.com/CLRX/CLRX-mirror.git; cmake -DCMAKE_INSTALL_PREFIX=$HOME/install/x86_64-Linux; make install\n  Example: -DCLRX_INCLUDE_DIRECTORY=$HOME/install/x86_64-Linux/include -DCLRX_AMDASM_LIBRARY=$HOME/install/x86_64-Linux/lib64/libCLRXAmdAsm.a -DCLRX_AMDBIN_LIBRARY=$HOME/install/x86_64-Linux/lib64/libCLRXAmdBin.a -DCLRX_UTILS_LIBRARY=$HOME/install/x86_64-Linux/lib64/libCLRXUtils.a")
  endif()
endif()

# XPM CUDA GPU Client

if (CUDA_FOUND)
  # Try CUDAToolkit imported targets; fallback to manual library lookup if unavailable
  if (CUDAToolkit_FOUND)
    add_executable(xpmclientnv
      adl.cpp
      baseclient.cpp
      cudautil.cpp
      loguru.cpp
      prime.cpp
      sha256.cpp
      zmqextras.cpp
      xpm/cuda/xpmclient.cpp
      xpm/cuda/benchmarks.cpp
      xpm/rpc/common/getblocktemplate.cpp
      xpm/rpc/common/primecoin.cpp
      xpm/rpc/common/system.cpp
      ${ProtoSources}
    )

    target_include_directories(xpmclientnv PUBLIC
      ${CMAKE_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/xpm/cuda
      ${CUDAToolkit_INCLUDE_DIRS}
      ${CMAKE_SOURCE_DIR}/xpm/rpc/blkmaker
      ${CMAKE_SOURCE_DIR}/xpm/rpc/common
      ${CMAKE_SOURCE_DIR}/xpm/rpc/include
    )

    target_link_libraries(xpmclientnv
      ${LIBRARIES}
      CUDA::cuda_driver
      CUDA::nvrtc
      blkmaker
      curl
      jansson
      crypto
      ssl
      stdc++
    )
  else()
    # Legacy path: manually locate libcuda and libnvrtc
    find_library(CUDA_driver_LIBRARY cuda
      ${CUDA_TOOLKIT_ROOT_DIR}/lib64
      ${CUDA_TOOLKIT_ROOT_DIR}/lib/x64
      /usr/local/cuda/lib64
      /usr/lib/x86_64-linux-gnu
    )
    find_library(CUDA_nvrtc_LIBRARY nvrtc
      ${CUDA_TOOLKIT_ROOT_DIR}/lib64
      ${CUDA_TOOLKIT_ROOT_DIR}/lib/x64
      /usr/local/cuda/lib64
      /usr/lib/x86_64-linux-gnu
    )

    if (NOT CUDA_driver_LIBRARY)
      message(FATAL_ERROR "CUDA driver library (libcuda) not found")
    endif()
    if (NOT CUDA_nvrtc_LIBRARY)
      message(FATAL_ERROR "CUDA NVRTC library (libnvrtc) not found")
    endif()

    add_executable(xpmclientnv
      adl.cpp
      baseclient.cpp
      cudautil.cpp
      loguru.cpp
      prime.cpp
      sha256.cpp
      zmqextras.cpp
      xpm/cuda/xpmclient.cpp
      xpm/cuda/benchmarks.cpp
      xpm/rpc/common/getblocktemplate.cpp
      xpm/rpc/common/primecoin.cpp
      xpm/rpc/common/system.cpp
      ${ProtoSources}
    )

    target_include_directories(xpmclientnv PUBLIC
      ${CMAKE_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/xpm/cuda
      ${CUDA_INCLUDE_DIRS}
      ${CMAKE_SOURCE_DIR}/xpm/rpc/blkmaker
      ${CMAKE_SOURCE_DIR}/xpm/rpc/common
      ${CMAKE_SOURCE_DIR}/xpm/rpc/include
    )

    target_link_libraries(xpmclientnv
      ${LIBRARIES}
      ${CUDA_driver_LIBRARY}
      ${CUDA_nvrtc_LIBRARY}
      blkmaker
      curl
      jansson
      crypto
      ssl
      stdc++
    )
  endif()

  if (STATIC_BUILD)
    set_target_properties(xpmclientnv PROPERTIES LINK_SEARCH_END_STATIC 1)
  endif()

  # Post-build: copy CUDA kernel sources for NVRTC runtime compilation
  file(GLOB CUDA_KERNEL_SOURCES "${CMAKE_SOURCE_DIR}/xpm/cuda/*.cu")
  add_custom_command(TARGET xpmclientnv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/xpm/cuda
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CUDA_KERNEL_SOURCES}
      ${CMAKE_BINARY_DIR}/xpm/cuda/
    COMMENT "Copying CUDA kernel source files to build directory for NVRTC runtime compilation"
  )
else()
  message(STATUS "CUDA not found. Skipping CUDA client (xpmclientnv).\n  Install hint: sudo apt install nvidia-cuda-toolkit or visit https://developer.nvidia.com/cuda-downloads")
endif()
  
# ZCash GPU Client
if (EQUIHASH_ENABLED)
  find_package(OpenSSL REQUIRED)
  
  add_definitions(-DXINTREE -DWN=200 -DWK=9 -DRESTBITS=4) 

  add_executable(zcashgpuclient
    adl.cpp
    base58.cpp
    baseclient.cpp
    loguru.cpp
    opencl.cpp
    sha256.cpp
    zmqextras.cpp
    zcash/zcashgpuclient.cpp
    zcash/equihash_original.cpp
    ${ProtoSources}
  )

  target_link_libraries(zcashgpuclient ${LIBRARIES} ${OpenCL_LIBRARIES})

  if (STATIC_BUILD)
    set_target_properties(zcashgpuclient PROPERTIES LINK_SEARCH_END_STATIC 1)
  endif()
endif()

# Copy CUDA config.txt to build directory to avoid manual copying
if (EXISTS ${CMAKE_SOURCE_DIR}/xpm/cuda/config.txt)
  file(COPY
    ${CMAKE_SOURCE_DIR}/xpm/opencl/config.txt
    DESTINATION ${CMAKE_BINARY_DIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
  )
endif()

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
find_package(Protobuf REQUIRED)
find_package(OpenCL)

if (NOT CMAKE_CROSSCOMPILING)
  find_package(CUDA)
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
  ${CLRX_INCLUDE_DIRECTORIES}
)

target_link_libraries(xpmclient
  ${LIBRARIES}
  ${OpenCL_LIBRARIES}
  ${CLRX_LIBRARIES}
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

# XPM CUDA GPU Client

if (CUDA_FOUND)
  find_library(CUDA_driver_LIBRARY cuda
    ${CUDA_TOOLKIT_ROOT_DIR}/lib64
    ${CUDA_TOOLKIT_ROOT_DIR}/lib/x64
  )
  find_library(CUDA_nvrtc_LIBRARY nvrtc
    ${CUDA_TOOLKIT_ROOT_DIR}/lib64
    ${CUDA_TOOLKIT_ROOT_DIR}/lib/x64
  )

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
  
  if (STATIC_BUILD)
    set_target_properties(xpmclientnv PROPERTIES LINK_SEARCH_END_STATIC 1)
  endif()
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

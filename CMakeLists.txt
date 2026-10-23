cmake_minimum_required(VERSION 3.20)

set(NATIVECPU_BITCODE_TAG "nightly-2026-10-16")
set(NATIVE_CPU_PASSES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Build standalone against an installed LLVM, or as an LLVM external project
# where the surrounding LLVM build already provides the CMake modules and
# compiler settings.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(NativeCPUPasses LANGUAGES C CXX)

  find_package(LLVM REQUIRED CONFIG)
  message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "${LLVM_BUILD_TYPE}" CACHE STRING "" FORCE)
  endif()

  list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
  include(AddLLVM)
  include(HandleLLVMOptions)

  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
  add_definitions(${LLVM_DEFINITIONS_LIST})
  include_directories(${LLVM_INCLUDE_DIRS})
endif()

if(NOT LLVM_ENABLE_PLUGINS)
  message(FATAL_ERROR
    "This LLVM was built without plugin support (LLVM_ENABLE_PLUGINS=OFF).")
endif()

include_directories(${NATIVE_CPU_PASSES_SOURCE_DIR}/llvm/include)

option(NATIVECPU_BITCODE_DOWNLOAD
  "Download the SYCL Native CPU bitcode from hipcpu-artifacts" ON)
set(NATIVECPU_BITCODE_FILE "" CACHE FILEPATH
  "Local libsycl-nativecpu_utils.bc to install instead of downloading")

if(NATIVECPU_BITCODE_FILE)
  if(NOT EXISTS "${NATIVECPU_BITCODE_FILE}")
    message(FATAL_ERROR
      "NATIVECPU_BITCODE_FILE does not exist: ${NATIVECPU_BITCODE_FILE}")
  endif()
  set(_nativecpu_bitcode_file "${NATIVECPU_BITCODE_FILE}")
elseif(NATIVECPU_BITCODE_DOWNLOAD)
  include(FetchContent)
  FetchContent_Declare(nativecpu-bitcode
    GIT_REPOSITORY https://github.com/st-czc/hipcpu-artifacts.git
    GIT_TAG ${NATIVECPU_BITCODE_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nativecpu-bitcode)
  set(_nativecpu_bitcode_dir
    "${nativecpu-bitcode_SOURCE_DIR}/bitcode/${NATIVECPU_BITCODE_TAG}")
  set(_nativecpu_bitcode_file
    "${_nativecpu_bitcode_dir}/libsycl-nativecpu_utils.bc")
else()
  message(STATUS "SYCL Native CPU bitcode download disabled; not installing it")
endif()

if(_nativecpu_bitcode_file)
  install(FILES "${_nativecpu_bitcode_file}" DESTINATION spvcpu/bitcode)
endif()

add_subdirectory(llvm/lib/SYCLNativeCPUUtils)

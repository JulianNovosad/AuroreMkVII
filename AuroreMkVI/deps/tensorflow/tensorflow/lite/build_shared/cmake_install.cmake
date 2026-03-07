# Install script for directory: /home/pi/Aurore/deps/tensorflow/tensorflow/lite

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/pi/Aurore/artifacts/tflite")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/eigen-build/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/farmhash-build/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/fft2d-build/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/flatbuffers-build/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/gemmlowp-build/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/cpuinfo-build/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/ml_dtypes-build/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/ruy-build/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/pthreadpool/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/tools/cmake/modules/xnnpack/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtensorflow-lite.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtensorflow-lite.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtensorflow-lite.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/libtensorflow-lite.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtensorflow-lite.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtensorflow-lite.so")
    file(RPATH_CHANGE
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtensorflow-lite.so"
         OLD_RPATH "/home/pi/Aurore/artifacts/abseil/lib:/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/farmhash-build:/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/fft2d-build:/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/xnnpack-build:/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/gemmlowp-build:/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/pthreadpool:/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/cpuinfo-build:"
         NEW_RPATH "")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libtensorflow-lite.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/acceleration/configuration" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/acceleration/configuration/delegate_registry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/acceleration/configuration" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/acceleration/configuration/nnapi_plugin.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/acceleration/configuration" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/acceleration/configuration/stable_delegate_registry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/api/error_reporter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/api/flatbuffer_conversions.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/api/op_resolver.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/api/op_resolver_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/api/profiler.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/api/tensor_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/api/verifier.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/c/async_kernel.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/c/async_signature_runner.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/c/internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/c/task.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/c/types.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/interop/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/interop/c/attribute_map.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/interop/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/interop/c/constants.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/interop/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/interop/c/types.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/interop" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/interop/attribute_map_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/interop" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/interop/reconcile_fns.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async/interop" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/interop/variant.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/async_kernel_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/async_signature_runner.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/async_subgraph.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/backend_async_kernel_interface.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/async" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/async/task_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/c/builtin_op_data.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/c/c_api.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/c/c_api_experimental.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/c/c_api_opaque.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/c/c_api_types.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/c/common.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/c/operator.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/experimental/acceleration/configuration" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/experimental/acceleration/configuration/delegate_registry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/experimental/acceleration/configuration" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/experimental/acceleration/configuration/stable_delegate_registry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/kernels/builtin_op_kernels.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/kernels/register.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/create_op_resolver.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/interpreter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/interpreter_builder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/macros.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/model.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/model_builder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/model_building.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/signature_runner.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/subgraph.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/tools" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/tools/verifier.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/core/tools" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/core/tools/verifier_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/builtin_op_data.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/c_api.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/c_api_experimental.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/c_api_for_testing.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/c_api_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/c_api_opaque.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/c_api_opaque_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/c_api_types.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/common.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/c/common_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/delegates/external" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/delegates/external/external_delegate.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/delegates/external" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/delegates/external/external_delegate_interface.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/delegates" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/delegates/interpreter_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/delegates" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/delegates/serialization.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/delegates" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/delegates/telemetry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/delegates" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/delegates/utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/experimental/resource" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/experimental/resource/cache_buffer.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/experimental/resource" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/experimental/resource/initialization_status.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/experimental/resource" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/experimental/resource/lookup_interfaces.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/experimental/resource" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/experimental/resource/lookup_util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/experimental/resource" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/experimental/resource/resource_base.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/experimental/resource" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/experimental/resource/resource_variable.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/experimental/resource" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/experimental/resource/static_hashtable.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/4bit" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/4bit/fully_connected_common.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/4bit" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/4bit/fully_connected_reference.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/4bit" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/4bit/fully_connected_reference_impl.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/add.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/depthwise_conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/depthwise_conv_3x3_filter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/depthwise_conv_hybrid.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/depthwise_conv_hybrid_3x3_filter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/fully_connected.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/leaky_relu.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/lut.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/mean.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/mul.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/pooling.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/sub.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/integer_ops/transpose_conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized/sparse_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/sparse_ops/fully_connected.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/avx2_quantization_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/batch_matmul.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/cpu_check.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/depthwiseconv_3x3_filter_common.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/depthwiseconv_float.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/depthwiseconv_multithread.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/depthwiseconv_uint8.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/depthwiseconv_uint8_3x3_filter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/depthwiseconv_uint8_transitional.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/eigen_spatial_convolutions.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/fully_connected_4bit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/im2col_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/legacy_optimized_ops.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/multithreaded_conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/neon_check.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/neon_tensor_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/neon_tensor_utils_impl.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/optimized_ops.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/optimized_ops_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/reduce.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/reduce_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/resize_bilinear.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/sse_check.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/sse_tensor_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/optimized" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/optimized/sse_tensor_utils_impl.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/add.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/depthwise_conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/dequantize.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/fully_connected.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/l2normalization.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/log_softmax.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/logistic.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/lut.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/mean.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/mul.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/pooling.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/tanh.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/integer_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/integer_ops/transpose_conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference/sparse_ops" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/sparse_ops/fully_connected.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/add.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/add_n.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/arg_min_max.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/batch_matmul.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/batch_to_space_nd.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/binary_function.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/broadcast_args.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/broadcast_to.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/cast.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/ceil.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/comparisons.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/concatenation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/conv3d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/conv3d_transpose.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/cumsum.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/densify.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/depth_to_space.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/depthwiseconv_float.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/depthwiseconv_uint8.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/dequantize.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/div.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/elu.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/exp.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/fill.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/floor.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/floor_div.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/floor_mod.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/fully_connected.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/gather.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/gelu.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/hard_swish.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/l2normalization.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/leaky_relu.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/legacy_reference_ops.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/log_softmax.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/logistic.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/lstm_cell.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/maximum_minimum.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/mul.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/neg.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/non_max_suppression.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/pad.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/pooling.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/portable_tensor_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/portable_tensor_utils_impl.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/prelu.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/process_broadcast_shapes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/quantize.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/reduce.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/reference_ops.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/requantize.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/resize_bilinear.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/resize_nearest_neighbor.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/round.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/select.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/slice.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/softmax.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/space_to_batch_nd.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/space_to_depth.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/strided_slice.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/string_comparisons.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/sub.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/svdf.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/tanh.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/transpose.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal/reference" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reference/transpose_conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/common.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/compatibility.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/constants.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/cppmath.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/kernel_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/legacy_types.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/max.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/mfcc.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/mfcc_dct.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/mfcc_mel_filterbank.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/min.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/opaque_tensor_ctypes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/portable_tensor.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/portable_tensor_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/quantization_util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/reduce_common.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/runtime_shape.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/spectrogram.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/strided_slice_logic.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/tensor.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/tensor_ctypes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/tensor_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/transpose_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/internal/types.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/builtin_op_kernels.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/control_flow_common.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_context.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_gemm.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_gemm_custom_gemv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_gemm_eigen.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_gemm_gemmlowp.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_gemm_params.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_gemm_ruy.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_gemm_x86.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/cpu_backend_threadpool.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/custom_ops_register.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/dequantize.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/eigen_support.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/fully_connected.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/gru_cell.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/kernel_util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/lstm_eval.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/lstm_shared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/op_macros.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/padding.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/register.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/register_ref.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/rng_util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/stablehlo_elementwise.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/kernels" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/kernels/tensor_slice_util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/profiling" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/profiling/root_profiler.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/profiling/telemetry" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/profiling/telemetry/profiler.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/profiling/telemetry/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/profiling/telemetry/c/telemetry_setting_internal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/profiling/telemetry/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/profiling/telemetry/c/profiler.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/profiling/telemetry/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/profiling/telemetry/c/telemetry_setting.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/profiling/telemetry" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/profiling/telemetry/telemetry_status.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/internal" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/internal/signature_def.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/schema" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/schema/conversion_metadata_generated.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/schema" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/schema/schema_generated.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./allocation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./arena_planner.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./array.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./builtin_op_data.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./builtin_ops.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./context.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./context_util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./create_op_resolver.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./error_reporter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./external_cpu_backend_context.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./graph_info.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./interpreter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./interpreter_builder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./interpreter_options.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./logger.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./memory_planner.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./minimal_logging.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./model.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./model_builder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./mutable_op_resolver.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./mutable_op_resolver_utils.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./namespace.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./op_resolver.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./optional_debug_tools.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./portable_type_to_tflitetype.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./shared_library.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./signature_runner.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./simple_memory_arena.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./simple_planner.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./stateful_error_reporter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./stderr_reporter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./string_type.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./string_util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./tensorflow_profiler_logger.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./tflite_with_xnnpack_optional.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./type_to_tflitetype.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/./version.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/allocation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/core/api/error_reporter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/core/api" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/core/api/verifier.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/core/c" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/core/c/tflite_types.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/core/macros.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/core" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/core/model_builder_base.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/experimental/remat" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/experimental/remat/metadata_util.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/schema" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/schema/conversion_metadata_generated.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/schema" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/schema/schema_generated.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/utils" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/utils/control_edges.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tensorflow/lite/../compiler/mlir/lite/utils" TYPE FILE FILES "/home/pi/Aurore/deps/tensorflow/tensorflow/compiler/mlir/lite/utils/string_utils.h")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/profiling/proto/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/example_proto_generated/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/tools/benchmark/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/examples/label_image/cmake_install.cmake")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()

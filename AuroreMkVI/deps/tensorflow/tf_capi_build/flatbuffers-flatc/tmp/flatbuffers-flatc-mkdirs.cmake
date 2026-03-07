# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers")
  file(MAKE_DIRECTORY "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers")
endif()
file(MAKE_DIRECTORY
  "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers-flatc/src/flatbuffers-flatc-build"
  "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers-flatc"
  "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers-flatc/tmp"
  "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers-flatc/src/flatbuffers-flatc-stamp"
  "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers-flatc/src"
  "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers-flatc/src/flatbuffers-flatc-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers-flatc/src/flatbuffers-flatc-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/pi/Aurore/deps/tensorflow/tf_capi_build/flatbuffers-flatc/src/flatbuffers-flatc-stamp${cfgdir}") # cfgdir has leading slash
endif()

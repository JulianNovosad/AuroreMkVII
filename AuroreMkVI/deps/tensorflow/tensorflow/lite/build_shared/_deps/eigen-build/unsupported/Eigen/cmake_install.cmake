# Install script for directory: /home/pi/Aurore/deps/eigen/unsupported/Eigen

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

if(CMAKE_INSTALL_COMPONENT STREQUAL "Devel" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/eigen3/unsupported/Eigen" TYPE FILE FILES
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/AdolcForward"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/AlignedVector3"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/ArpackSupport"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/AutoDiff"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/BVH"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/EulerAngles"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/FFT"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/IterativeSolvers"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/KroneckerProduct"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/LevenbergMarquardt"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/MatrixFunctions"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/MPRealSupport"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/NNLS"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/NonLinearOptimization"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/NumericalDiff"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/OpenGLSupport"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/Polynomials"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/SparseExtra"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/SpecialFunctions"
    "/home/pi/Aurore/deps/eigen/unsupported/Eigen/Splines"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Devel" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/eigen3/unsupported/Eigen" TYPE DIRECTORY FILES "/home/pi/Aurore/deps/eigen/unsupported/Eigen/src" FILES_MATCHING REGEX "/[^/]*\\.h$")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/eigen-build/unsupported/Eigen/CXX11/cmake_install.cmake")

endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/pi/Aurore/deps/tensorflow/tensorflow/lite/build_shared/_deps/eigen-build/unsupported/Eigen/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()

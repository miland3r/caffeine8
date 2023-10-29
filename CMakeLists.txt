cmake_minimum_required(VERSION 3.10)

# Set the project name
project(Caffeine8)

# Set the default image paths
set(DEFAULT_IMAGE_PATH "${CMAKE_INSTALL_PREFIX}/share/caffeine8" CACHE STRING "Default path for XPM images")

# Configure a header file to pass the CMake settings to the source code
configure_file(
  "${PROJECT_SOURCE_DIR}/include/config.h.in"
  "${PROJECT_BINARY_DIR}/include/config.h"
)

# Add the include directory
include_directories(include ${PROJECT_BINARY_DIR}/include)

# Add subdirectories
add_subdirectory(src)

# Install assets
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/images/ DESTINATION ${DEFAULT_IMAGE_PATH})

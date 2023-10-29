# Find the required packages
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MAGICK++ REQUIRED IMPORTED_TARGET Magick++)

# Add executable
add_executable(caffeine8 caffeine8.cpp)

# Link libraries
target_link_libraries(caffeine8 PRIVATE PkgConfig::MAGICK++ ${X11_LIBRARIES} Xpm)

# Include directories for X11
target_include_directories(caffeine8 PRIVATE ${X11_INCLUDE_DIR})

# Install the executable to 'bin' directory under CMAKE_INSTALL_PREFIX
install(TARGETS caffeine8 DESTINATION bin)

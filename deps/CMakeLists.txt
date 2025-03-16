# blibs

add_library(blibs INTERFACE)
target_include_directories(blibs INTERFACE "blibs")

# minicoro

add_library(minicoro INTERFACE)
target_include_directories(minicoro INTERFACE "minicoro")

# liburing
if (LINUX)
	include("liburing.cmake")
endif ()


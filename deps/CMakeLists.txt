# blibs

add_library(blibs INTERFACE)
target_include_directories(blibs INTERFACE "blibs")

# minicoro

add_library(minicoro INTERFACE)
target_include_directories(minicoro INTERFACE "minicoro")

# liburing

if (LINUX)
	set(LIBURING_SOURCES
		"liburing/src/ffi.c"
		"liburing/src/queue.c"
		"liburing/src/register.c"
		"liburing/src/setup.c"
		"liburing/src/syscall.c"
		"liburing/src/version.c"
		"liburing/src/include/liburing/io_uring_version.h"  # Generated
		$<$<CONFIG:Debug>:liburing/src/sanitize.c>
	)
	add_library(liburing STATIC EXCLUDE_FROM_ALL "${LIBURING_SOURCES}")
	target_include_directories(liburing SYSTEM PUBLIC "liburing/src/include")
	target_compile_definitions(liburing PRIVATE
		"_GNU_SOURCE"
		$<$<CONFIG:Debug>:CONFIG_USE_SANITIZER>
	)
	set_target_properties(liburing PROPERTIES PREFIX "")
	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/liburing/src/include/liburing/io_uring_version.h
		COMMAND ./configure
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/liburing
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/liburing/configure
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/liburing/liburing.spec
	)
endif ()

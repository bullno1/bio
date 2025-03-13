# blibs

add_library(blibs INTERFACE)
target_include_directories(blibs INTERFACE "blibs")

# minicoro

add_library(minicoro INTERFACE)
target_include_directories(minicoro INTERFACE "minicoro")

# liburing

set(LIBURING_SOURCES
	"liburing/src/ffi.c"
	"liburing/src/queue.c"
	"liburing/src/register.c"
	"liburing/src/setup.c"
	"liburing/src/syscall.c"
	"liburing/src/version.c"
)
add_library(liburing STATIC EXCLUDE_FROM_ALL "${LIBURING_SOURCES}")
target_include_directories(liburing PUBLIC "liburing/src/include")
target_compile_definitions(liburing PRIVATE "_GNU_SOURCE")
set_target_properties(liburing PROPERTIES PREFIX "")

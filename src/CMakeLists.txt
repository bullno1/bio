set(COMMON_SOURCES
	"bio.c"
	"handle.c"
	"timer.c"
	"scheduler.c"
	"mailbox.c"
	"net.c"
	"logging.c"
	"logging/file.c"
	"thread.c"
	"service.c"
	"timer.c"
	"array.c"
	"minicoro.c"
)
set(LINUX_SOURCES
	"linux/platform.c"
	"linux/net.c"
	"linux/file.c"
)
set(WIN32_SOURCES
	"windows/platform.c"
	"windows/file.c"
	"windows/net.c"
	"windows/net/ws.c"
	"windows/net/pipe.c"
)
set(FREEBSD_SOURCES
	"freebsd/platform.c"
	"freebsd/file.c"
)
if (LINUX)
	add_library(bio STATIC ${COMMON_SOURCES} ${LINUX_SOURCES})
elseif (WIN32)
	add_library(bio STATIC ${COMMON_SOURCES} ${WIN32_SOURCES})
elseif (BSD STREQUAL "FreeBSD")
	add_library(bio STATIC ${COMMON_SOURCES} ${FREEBSD_SOURCES})
else ()
	message(SEND_ERROR "Unsupported platform")
endif ()
target_include_directories(bio PUBLIC "../include")
target_link_libraries(bio PRIVATE minicoro)

if (BUILD_MINICORO)
	target_compile_definitions(bio PRIVATE "BIO_BUILD_MINICORO=1")
endif ()

if (LINUX)
	target_link_libraries(bio PRIVATE liburing)
elseif (WIN32)
	target_link_libraries(bio PRIVATE "ws2_32.lib")
elseif (BSD)
	target_link_libraries(bio PRIVATE "stdthreads")
endif ()

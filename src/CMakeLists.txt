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
	"array.c"
	"minicoro.c"
)
set(LINUX_SOURCES
	"linux/platform.c"
	"linux/net.c"
	"linux/file.c"
)
set(WINDOWS_SOURCES
	"windows/platform.c"
)

if (LINUX)
	set(SOURCES ${COMMON_SOURCES} ${LINUX_SOURCES})
elseif (WIN32)
	set(SOURCES ${COMMON_SOURCES} ${WINDOWS_SOURCES})
endif ()

add_library(bio STATIC ${SOURCES})
target_include_directories(bio PUBLIC "../include")
target_link_libraries(bio PRIVATE minicoro)

if (BUILD_MINICORO)
	target_compile_definitions(bio PRIVATE "BIO_BUILD_MINICORO=1")
endif ()

if (LINUX)
	target_link_libraries(bio PRIVATE liburing)
endif ()

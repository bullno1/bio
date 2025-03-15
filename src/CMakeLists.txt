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
	"hthread.c"
	"minicoro.c"
)
set(LINUX_SOURCES
	"linux/platform.c"
	"linux/net.c"
	"linux/file.c"
)
add_library(bio STATIC ${COMMON_SOURCES} ${LINUX_SOURCES})
target_include_directories(bio PUBLIC "../include")
target_link_libraries(bio PRIVATE minicoro)

if (BUILD_MINICORO)
	target_compile_definitions(bio PRIVATE "BIO_BUILD_MINICORO=1")
endif ()

if (LINUX)
	target_link_libraries(bio PRIVATE liburing)
endif ()

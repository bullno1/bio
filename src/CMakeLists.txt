set(COMMON_SOURCES
	"bio.c"
	"mailbox.c"
	"net.c"
	"log.c"
	"log/file.c"
	"minicoro.c"
)
set(LINUX_SOURCES
	"linux/common.c"
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

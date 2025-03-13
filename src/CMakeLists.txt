set(SOURCES
	"bio.c"
	"mailbox.c"
	"minicoro.c"
)
add_library(bio STATIC ${SOURCES})
target_include_directories(bio PUBLIC "../include")
target_link_libraries(bio PRIVATE minicoro)

if (BUILD_MINICORO)
	target_compile_definitions(bio PRIVATE "BIO_BUILD_MINICORO=1")
endif ()

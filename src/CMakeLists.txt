set(SOURCES
	"bio.c"
)
add_library(bio STATIC ${SOURCES})
target_include_directories(bio PUBLIC "../include")

set(SOURCES
	"main.c"
	"handle.c"
	"common.c"
)
add_executable(tests ${SOURCES})
target_link_libraries(tests PRIVATE bio blibs)

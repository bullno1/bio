set(SOURCES
	"main.c"
	"common.c"
	"handle.c"
	"coro.c"
	"mailbox.c"
	"net.c"
	"file.c"
	"timer.c"
)
add_executable(tests ${SOURCES})
target_link_libraries(tests PRIVATE bio blibs)

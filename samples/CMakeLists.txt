add_executable(echo "echo.c")
target_link_libraries(echo PRIVATE bio)

add_executable(signal "signal.c")
target_link_libraries(signal PRIVATE bio)

if (LINUX)
	target_link_options(echo PRIVATE $<$<CONFIG:RelWithDebInfo>:-static>)
endif ()

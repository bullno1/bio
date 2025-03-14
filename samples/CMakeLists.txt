add_executable(echo "echo.c")
target_link_libraries(echo PRIVATE bio)

if (LINUX)
	target_link_options(echo PRIVATE $<$<CONFIG:RelWithDebInfo>:-static>)
endif ()

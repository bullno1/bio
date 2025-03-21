set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")
set(CLANG_SANITIZE "-fsanitize=address,undefined -fno-sanitize=vptr")
set(CMAKE_C_FLAGS_DEBUG_INIT ${CLANG_SANITIZE})
set(CMAKE_CXX_FLAGS_DEBUG_INIT ${CLANG_SANITIZE})
add_link_options("-fuse-ld=mold" "-Wl,--separate-debug-file")

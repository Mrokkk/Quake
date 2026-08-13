cmake_minimum_required(VERSION 3.14)

project(quake)

option(SANITIZE "Enable ASan and UBsan" OFF)

if(NOT BACKEND)
    set(BACKEND "X11" CACHE STRING "Used platform")
endif()

message(STATUS "Backend:               ${BACKEND}")
message(STATUS "Enable ASan and UBsan: ${SANITIZE}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

find_program(CCACHE ccache)

if(CCACHE)
    message(STATUS "Using ccache: ${CCACHE}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE}")
    set(CMAKE_CXX_FLAGS "-fdiagnostics-color=always")
endif()

# General
add_compile_options(-pipe -std=gnu99 -ggdb3)

# Optimizations
add_compile_options(-O3 -ffast-math -funroll-loops -fexpensive-optimizations -fomit-frame-pointer)

# Diagnostics
add_compile_options(-Wall -Wextra -Wno-trigraphs -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-missing-field-initializers)

add_compile_definitions(-Dstricmp=strcasecmp)

if(SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

add_executable(${PROJECT_NAME}

    src/cd_linux.c
    src/chase.c
    src/cl_demo.c
    src/cl_input.c
    src/cl_main.c
    src/cl_parse.c
    src/cl_tent.c
    src/cmd.c
    src/common.c
    src/console.c
    src/crc.c
    src/cvar.c
    src/d_edge.c
    src/d_fill.c
    src/d_init.c
    src/d_modech.c
    src/d_part.c
    src/d_polyse.c
    src/d_scan.c
    src/d_sky.c
    src/d_sprite.c
    src/d_surf.c
    src/d_vars.c
    src/d_zpoint.c
    src/draw.c
    src/host.c
    src/host_cmd.c
    src/keys.c
    src/mathlib.c
    src/menu.c
    src/model.c
    src/net_bsd.c
    src/net_dgrm.c
    src/net_loop.c
    src/net_main.c
    src/net_udp.c
    src/net_vcr.c
    src/nonintel.c
    src/pr_cmds.c
    src/pr_edict.c
    src/pr_exec.c
    src/r_aclip.c
    src/r_alias.c
    src/r_bsp.c
    src/r_draw.c
    src/r_edge.c
    src/r_efrag.c
    src/r_light.c
    src/r_main.c
    src/r_misc.c
    src/r_part.c
    src/r_sky.c
    src/r_sprite.c
    src/r_surf.c
    src/r_vars.c
    src/sbar.c
    src/screen.c
    src/sv_main.c
    src/sv_move.c
    src/sv_phys.c
    src/sv_user.c
    src/sys_linux.c
    src/view.c
    src/wad.c
    src/world.c
    src/zone.c

)

if(SOUND)
    target_sources(${PROJECT_NAME} PUBLIC
        src/snd_dma.c
        src/snd_linux.c
        src/snd_mem.c
        src/snd_mix.c
    )
else()
    target_sources(${PROJECT_NAME} PUBLIC src/snd_null.c)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC m)

if(SANITIZE)
    target_link_libraries(${PROJECT_NAME} PUBLIC asan ubsan)
endif()

if(BACKEND STREQUAL "X11")

    find_package(X11 REQUIRED)

    target_sources(${PROJECT_NAME} PUBLIC src/vid_x.c)

    target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PUBLIC ${X11_LIBRARIES})

else()
    message(FATAL_ERROR "Unsupported backend: ${BACKEND}")
endif()

include(FindPkgConfig)
pkg_check_modules(BACKTRACE libbacktrace)

if(BACKTRACE_FOUND)
    target_compile_options(${PROJECT_NAME} PUBLIC "-DUSE_BACKTRACE")
    target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${BACKTRACE_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PUBLIC ${BACKTRACE_LIBRARIES})
    target_link_directories(${PROJECT_NAME} PUBLIC ${BACKTRACE_LIBRARY_DIRS})
endif()

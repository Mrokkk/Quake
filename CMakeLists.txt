cmake_minimum_required(VERSION 3.14)

project(quake C)

include(GNUInstallDirs)
include(cmake/build_types.cmake)
include(cmake/ccache.cmake)
include(cmake/compile_commands.cmake)

option(DEBUG_INFO  "Enable debug info"             ${DEF_DEBUG_INFO})
option(OPTIMIZE    "Enable compiler optimizations" ${DEF_OPTIMIZE})
option(LTO         "Enable Link Time Optimization" ${DEF_LTO})
option(SANITIZE    "Enable ASan and UBsan"         OFF)
option(PROFILE     "Enable profiling"              OFF)

if(NOT PLATFORM)
    set(PLATFORM "X11" CACHE STRING "Used platform")
endif()

# General
add_compile_options(-pipe -std=gnu99)

# Debug info
if(DEBUG_INFO)
    add_compile_options(-ggdb3)
endif()

# Optimizations
if(OPTIMIZE)
    add_compile_options(-O3 -ffast-math -funroll-loops -march=native)

    if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options(-fexpensive-optimizations)
    endif()
endif()

if(LTO)
    add_compile_options(-flto=auto)
endif()

if(PROFILE OR SANITIZE)
    add_compile_options(-fno-omit-frame-pointer)
else()
    add_compile_options(-fomit-frame-pointer)
endif()

if(SANITIZE)
    add_compile_options(-fsanitize=address,undefined)
endif()

add_compile_options(-include "${PROJECT_SOURCE_DIR}/src/cpp_interop.h")

# Diagnostics
add_compile_options(-Wall -Wextra -Wno-trigraphs -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-missing-field-initializers)

add_compile_definitions(-Dstricmp=strcasecmp)

add_executable(${PROJECT_NAME}

    src/cd_null.c
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
    src/sys_unix.c
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

if(SANITIZE)
    target_link_libraries(${PROJECT_NAME} PUBLIC asan ubsan)
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC m)

include(cmake/platform_${PLATFORM}.cmake)

include(FindPkgConfig)
pkg_check_modules(BACKTRACE libbacktrace)

if(BACKTRACE_FOUND)
    target_compile_definitions(${PROJECT_NAME} PUBLIC "-DUSE_BACKTRACE")
    target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${BACKTRACE_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PUBLIC ${BACKTRACE_LIBRARIES})
    target_link_directories(${PROJECT_NAME} PUBLIC ${BACKTRACE_LIBRARY_DIRS})
endif()

message(STATUS "################### Configuration ###################")
message(STATUS "Build type:                    ${CMAKE_BUILD_TYPE}")
message(STATUS "Enable debug info:             ${DEBUG_INFO}")
message(STATUS "Enable compiler optimizations: ${OPTIMIZE}")
message(STATUS "Enable Link Time Optimization: ${LTO}")
message(STATUS "Enable ASan and UBsan:         ${SANITIZE}")
message(STATUS "Enable profiling:              ${PROFILE}")
message(STATUS "Platform:                      ${PLATFORM}")
message(STATUS "Install binary dir:            ${CMAKE_INSTALL_FULL_BINDIR}")
message(STATUS "Install data dir:              ${CMAKE_INSTALL_FULL_DATADIR}")
message(STATUS "#####################################################")

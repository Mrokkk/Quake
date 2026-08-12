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

# General
add_compile_options(-pipe -std=gnu99 -ggdb3)

# Optimizations
add_compile_options(-O3 -ffast-math -funroll-loops -fexpensive-optimizations -fomit-frame-pointer)

# Diagnostics
add_compile_options(-Wall -Wno-trigraphs -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-missing-field-initializers)

add_compile_definitions(-Dstricmp=strcasecmp)

if(SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

add_executable(${PROJECT_NAME}

    cd_linux.c
    chase.c
    cl_demo.c
    cl_input.c
    cl_main.c
    cl_parse.c
    cl_tent.c
    cmd.c
    common.c
    console.c
    crc.c
    cvar.c
    d_edge.c
    d_fill.c
    d_init.c
    d_modech.c
    d_part.c
    d_polyse.c
    d_scan.c
    d_sky.c
    d_sprite.c
    d_surf.c
    d_vars.c
    d_zpoint.c
    draw.c
    host.c
    host_cmd.c
    keys.c
    mathlib.c
    menu.c
    model.c
    net_bsd.c
    net_dgrm.c
    net_loop.c
    net_main.c
    net_udp.c
    net_vcr.c
    nonintel.c
    pr_cmds.c
    pr_edict.c
    pr_exec.c
    r_aclip.c
    r_alias.c
    r_bsp.c
    r_draw.c
    r_edge.c
    r_efrag.c
    r_light.c
    r_main.c
    r_misc.c
    r_part.c
    r_sky.c
    r_sprite.c
    r_surf.c
    r_vars.c
    sbar.c
    screen.c
    snd_dma.c
    snd_linux.c
    snd_mem.c
    snd_mix.c
    sv_main.c
    sv_move.c
    sv_phys.c
    sv_user.c
    sys_linux.c
    view.c
    wad.c
    world.c
    zone.c

)

target_link_libraries(${PROJECT_NAME} PUBLIC m)

if(SANITIZE)
    target_link_libraries(${PROJECT_NAME} PUBLIC asan ubsan)
endif()

if(BACKEND STREQUAL "X11")

    find_package(X11 REQUIRED)

    target_sources(${PROJECT_NAME} PUBLIC vid_x.c)

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

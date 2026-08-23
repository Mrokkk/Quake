find_package(SDL2 REQUIRED)

target_sources(${PROJECT_NAME} PUBLIC
    src/snd_dma.c
    src/snd_sdl.c
    src/snd_mem.c
    src/snd_mix.c
)

target_link_libraries(${PROJECT_NAME} PUBLIC SDL2::SDL2)

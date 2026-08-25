find_package(SDL2 REQUIRED)

target_sources(${PROJECT_NAME} PUBLIC src/vid_sdl.c)

target_link_libraries(${PROJECT_NAME} PUBLIC SDL2::SDL2)

find_package(X11 REQUIRED)

target_sources(${PROJECT_NAME} PUBLIC src/vid_x.c)

target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC ${X11_LIBRARIES})

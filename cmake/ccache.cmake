find_program(CCACHE ccache)

if(CCACHE)
    message(STATUS "Using ccache: ${CCACHE}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE}")
    set(CMAKE_CXX_FLAGS "-fdiagnostics-color=always")
endif()

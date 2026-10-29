# Fails when a committed source or test names an absolute /home/<user> path.
# MUME-derived citations name the log corpus by its logical root, <powwow>
# (the player's powwow directory; logs live under <powwow>/logs/archives).
# Run as: cmake -DSOURCE_DIR=<repo root> -P check_no_home_paths.cmake
file(GLOB_RECURSE files "${SOURCE_DIR}/src/*" "${SOURCE_DIR}/tests/*")
set(self "${CMAKE_CURRENT_LIST_FILE}")
set(bad "")
foreach(f IN LISTS files)
    if(f STREQUAL self OR IS_DIRECTORY "${f}")
        continue()
    endif()
    file(STRINGS "${f}" hits REGEX "/home/[A-Za-z0-9_.-]+")
    foreach(h IN LISTS hits)
        list(APPEND bad "${f}: ${h}")
    endforeach()
endforeach()
if(bad)
    string(REPLACE ";" "\n  " msg "${bad}")
    message(FATAL_ERROR "absolute /home/<user> paths found (use <powwow> or a relative path):\n  ${msg}")
endif()

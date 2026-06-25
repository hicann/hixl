file(GLOB_RECURSE SRC_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.cc")
add_library(drvdsmi_host_stub SHARED ${SRC_FILES})
set_target_properties(drvdsmi_host_stub PROPERTIES OUTPUT_NAME drvdsmi_host)
target_link_libraries(drvdsmi_host_stub PRIVATE dl)

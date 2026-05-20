file(GLOB_RECURSE SRC_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.cc")
add_library(dcmi_stub SHARED ${SRC_FILES})
set_target_properties(dcmi_stub PROPERTIES OUTPUT_NAME dcmi)
target_link_libraries(dcmi_stub PRIVATE c_sec dl)

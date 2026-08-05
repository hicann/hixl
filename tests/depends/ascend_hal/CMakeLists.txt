file(GLOB_RECURSE SRC_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.cc")
add_library(hixl_ascend_hal_stub SHARED ${SRC_FILES})
set_target_properties(hixl_ascend_hal_stub PROPERTIES OUTPUT_NAME ascend_hal)
target_link_libraries(hixl_ascend_hal_stub PRIVATE dl)

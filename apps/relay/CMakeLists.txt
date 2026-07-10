cmake_minimum_required(VERSION 3.22)

# [OTA] Version = thời gian SỬA ĐỔI file mã nguồn mới nhất (KHÔNG phải giờ
# bấm build) — build lại mà không sửa gì thì version giữ nguyên (không tự OTA
# vô ích dù nội dung .bin giống hệt); chỉ khi thực sự sửa 1 file .c/.h thì
# version mới nhảy lên. So sánh 2 chuỗi YYYYMMDDHHMMSS bằng strncmp trong
# bmt_ota.c là biết ngay cái nào MỚI HƠN, tránh downgrade nhầm.
set(PROJECT_VER "00000000000000")
file(GLOB_RECURSE BMT_VER_SRC_FILES
    "${CMAKE_CURRENT_LIST_DIR}/main/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/main/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/main/CMakeLists.txt"
)
foreach(_bmt_src_file ${BMT_VER_SRC_FILES})
    file(TIMESTAMP "${_bmt_src_file}" _bmt_src_ts "%Y%m%d%H%M%S" UTC)
    if(_bmt_src_ts STRGREATER PROJECT_VER)
        set(PROJECT_VER "${_bmt_src_ts}")
    endif()
endforeach()
message(STATUS "PROJECT_VER (newest source mtime) = ${PROJECT_VER}")

# Bắt buộc: báo cho CMake biết "nếu bất kỳ file nào trong danh sách trên đổi
# mtime thì phải chạy lại configure" — thiếu dòng này thì sửa nội dung .c/.h
# xong build lại, ninja chỉ biên dịch lại file đó chứ KHÔNG tự tính lại
# PROJECT_VER (CMake configure không tự re-run chỉ vì nội dung 1 file đổi).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BMT_VER_SRC_FILES})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Relay)

# Copy .bin ra thu muc firmware/ cua repo de phuc vu OTA server.
# Override bang: idf.py -DBMT_OTA_DIR=/duong/dan/khac build
if(NOT DEFINED BMT_OTA_DIR)
    set(BMT_OTA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../firmware")
endif()
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BMT_OTA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/Relay.bin"
        "${BMT_OTA_DIR}/Relay.bin"
    COMMENT "Copied Relay.bin -> ${BMT_OTA_DIR}"
)
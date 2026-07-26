# [SECURITY] Copy cua zephyr/share/sysbuild/template/CMakeLists.txt.
#
# MCUboot chay nhu 1 Zephyr app RIENG (khong phai app "beacon" chinh), nen no
# KHONG tu doc boards/xiao_ble_nrf52840.overlay cua app beacon. Phai truyen
# tay overlay do vao build cua MCUboot qua bien CMake mcuboot_EXTRA_DTC_OVERLAY_FILE
# TRUOC khi find_package(Sysbuild) chay (no la thu tao ra target "mcuboot").
set(mcuboot_EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_LIST_DIR}/../boards/mcuboot_partitions.overlay
    CACHE INTERNAL ""
)

# Board mac dinh chon USB CDC-ACM lam console, nhung MCUboot khong bat USB
# stack -> link error "undefined reference to __device_dts_ord_N". MCUboot
# khong can console (chi verify + boot), xem mcuboot.conf.
set(mcuboot_EXTRA_CONF_FILE
    ${CMAKE_CURRENT_LIST_DIR}/../mcuboot.conf
    CACHE INTERNAL ""
)

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sysbuild LANGUAGES)

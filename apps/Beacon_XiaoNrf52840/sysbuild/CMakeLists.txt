# [SECURITY] Copy of zephyr/share/sysbuild/template/CMakeLists.txt.
#
# MCUboot runs as its own Zephyr app (separate from the "beacon" app),
# so it does NOT automatically read boards/xiao_ble_nrf52840.overlay
# from the beacon app. Pass that overlay into the MCUboot build
# explicitly via the CMake variable mcuboot_EXTRA_DTC_OVERLAY_FILE
# BEFORE find_package(Sysbuild) runs (that call creates the "mcuboot"
# target).
set(mcuboot_EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_LIST_DIR}/../boards/mcuboot_partitions.overlay
    CACHE INTERNAL ""
)

# The board's default serial backend is USB CDC-ACM, but MCUboot does
# not enable the USB stack -> link error "undefined reference to
# __device_dts_ord_N". MCUboot has no need for a console (just verify
# and boot), see mcuboot.conf.
set(mcuboot_EXTRA_CONF_FILE
    ${CMAKE_CURRENT_LIST_DIR}/../mcuboot.conf
    CACHE INTERNAL ""
)

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sysbuild LANGUAGES)

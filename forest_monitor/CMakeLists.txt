cmake_minimum_required(VERSION 3.16)

# Load the common defaults plus a git-ignored local override file for
# secrets (Wi-Fi password, Firebase auth) so they never land in a tracked
# file. ESP-IDF still auto-appends the per-target ".<target>" variant of
# each entry, so sdkconfig.defaults.esp32 / .esp32s3 keep applying.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(forest_monitor)

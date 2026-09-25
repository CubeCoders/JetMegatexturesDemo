# Include before ESP-IDF's project.cmake in each example.
get_filename_component(EXAMPLES_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(JET_CONFIG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main/firmware")
set(EXTRA_COMPONENT_DIRS
    "${EXAMPLES_ROOT}/components/Jet"
    "${EXAMPLES_ROOT}/components/LovyanGFX"
    "${EXAMPLES_ROOT}/components/esp32_jet")

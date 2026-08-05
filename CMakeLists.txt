# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)


project(esp-miner)

# Post-build step: automatically create merged binary (esp-miner-merged.bin)
add_custom_target(
    merge_bin_target
    COMMAND ${CMAKE_COMMAND} -E echo "Creating merged binary: esp-miner-merged.bin"
    COMMAND ${PYTHON} -m esptool --chip esp32s3 merge_bin
        --flash_mode dio --flash_size 16MB --flash_freq 80m
        0x0 build/bootloader/bootloader.bin
        0x8000 build/partition_table/partition-table.bin
        0x10000 build/esp-miner.bin
        0xf10000 build/ota_data_initial.bin
        -o esp-miner-merged.bin
    WORKING_DIRECTORY ${PROJECT_DIR}
    DEPENDS app
    COMMENT "Creating merged binary: esp-miner-merged.bin"
)

# Config file to merge. Set with: idf.py -DMERGE_CONFIG_CVS=config-401.cvs
# Default: config-401.cvs (standard Bitaxe config)
set(MERGE_CONFIG_CVS "config-401.cvs" CACHE STRING "Config CSV file to include in merged binary (e.g. config-401.cvs)")

# Generate config.bin from the selected .cvs file using ESP-IDF's nvs_partition_gen
set(CONFIG_BIN_OUTPUT "${PROJECT_DIR}/config.bin")
add_custom_command(
    OUTPUT ${CONFIG_BIN_OUTPUT}
    COMMAND ${PYTHON} "$ENV{IDF_PATH}/components/nvs_flash/tools/nvs_partition_gen.py"
        generate ${MERGE_CONFIG_CVS} ${CONFIG_BIN_OUTPUT} --size 0x1000
    WORKING_DIRECTORY ${PROJECT_DIR}
    DEPENDS ${PROJECT_DIR}/${MERGE_CONFIG_CVS}
    COMMENT "Generating config.bin from ${MERGE_CONFIG_CVS}"
)

# Merged binary with config.bin included (for custom boards)
add_custom_target(
    merge_bin_target_with_config
    COMMAND ${CMAKE_COMMAND} -E echo "Creating merged binary with config from: ${MERGE_CONFIG_CVS}"
    COMMAND ${PYTHON} -m esptool --chip esp32s3 merge_bin
        --flash_mode dio --flash_size 16MB --flash_freq 80m
        0x0 build/bootloader/bootloader.bin
        0x8000 build/partition_table/partition-table.bin
        0x9000 ${CONFIG_BIN_OUTPUT}
        0x10000 build/esp-miner.bin
        0xf10000 build/ota_data_initial.bin
        -o esp-miner-merged-with-config.bin
    WORKING_DIRECTORY ${PROJECT_DIR}
    DEPENDS app ${CONFIG_BIN_OUTPUT}
    COMMENT "Creating merged binary with config from ${MERGE_CONFIG_CVS}"
)

# Update-only binary (firmware + OTA in hex format, for OTA updates)
add_custom_target(
    merge_bin_target_update
    COMMAND ${CMAKE_COMMAND} -E echo "Creating update binary: esp-miner-update.bin"
    COMMAND ${PYTHON} -m esptool --chip esp32s3 merge_bin
        --flash_mode dio --flash_size 16MB --flash_freq 80m --format hex
        0x10000 build/esp-miner.bin
        0xf10000 build/ota_data_initial.bin
        -o esp-miner-update.bin
    WORKING_DIRECTORY ${PROJECT_DIR}
    DEPENDS app
    COMMENT "Creating update binary: esp-miner-update.bin"
)

# Generate merged binaries for all config-*.bin files (like merge_bin_all.sh -c)
file(GLOB CONFIG_BINS "${PROJECT_DIR}/config-*.bin")
if(CONFIG_BINS)
    add_custom_target(
        merge_bin_target_all_configs
        COMMAND ${CMAKE_COMMAND} -E echo "Creating merged binaries for all configs..."
        COMMAND ${PYTHON} -c "
import glob, subprocess, sys
configs = glob.glob('config-*.bin')
for cfg in sorted(configs):
    out = cfg.replace('.bin', '-merged.bin')
    cmd = [
        sys.executable, '-m', 'esptool', '--chip', 'esp32s3', 'merge_bin',
        '--flash_mode', 'dio', '--flash_size', '16MB', '--flash_freq', '80m',
        '0x0', 'build/bootloader/bootloader.bin',
        '0x8000', 'build/partition_table/partition-table.bin',
        '0x9000', cfg,
        '0x10000', 'build/esp-miner.bin',
        '0xf10000', 'build/ota_data_initial.bin',
        '-o', out
    ]
    print(f'Creating {out}')
    subprocess.run(cmd, check=True)
print('All merged binaries created.')
"
        WORKING_DIRECTORY ${PROJECT_DIR}
        DEPENDS app
        COMMENT "Creating merged binaries for all config-*.bin files"
    )
endif()


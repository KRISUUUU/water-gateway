cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(water_gateway)

# Package web assets from `web/` into the `storage` SPIFFS partition image
# and include that image in `idf.py flash`.
spiffs_create_partition_image(storage web FLASH_IN_PROJECT)

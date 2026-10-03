# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/web/index.html"
                                      "${CMAKE_SOURCE_DIR}/web/style.css"
                                      "${CMAKE_SOURCE_DIR}/web/app.js")

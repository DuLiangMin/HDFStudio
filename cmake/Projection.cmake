# Isolated PROJ dependency; deliberately do not switch the application's toolchain.
if(NOT PROJ_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/ext/vcpkg/installed/x64-windows/share/proj/proj-config.cmake")
  set(PROJ_DIR "${CMAKE_SOURCE_DIR}/ext/vcpkg/installed/x64-windows/share/proj" CACHE PATH "Pinned PROJ CMake package")
endif()
if(PROJ_DIR)
  get_filename_component(HDF_PROJ_PREFIX "${PROJ_DIR}/../.." ABSOLUTE)
  set(SQLite3_ROOT "${HDF_PROJ_PREFIX}")
endif()
find_package(PROJ 9 CONFIG REQUIRED)
get_filename_component(HDF_PROJ_PREFIX "${PROJ_DIR}/../.." ABSOLUTE)
set(HDF_PROJ_DATA "${HDF_PROJ_PREFIX}/share/proj" CACHE PATH "Matching offline PROJ database")
file(SHA256 "${CMAKE_SOURCE_DIR}/resources/coastlines/low/coastlines_l.json" HDF_COASTLINE_HASH)
add_library(map_core STATIC src/core/map_projection.cpp src/core/map_projection.hpp src/core/geo_grid.cpp src/core/geo_grid.hpp src/core/map_render.cpp src/core/map_render.hpp src/core/coastline.cpp src/core/coastline.hpp)
target_compile_definitions(map_core PRIVATE HDFSTUDIO_COASTLINE_SHA256="${HDF_COASTLINE_HASH}")
target_link_libraries(map_core PUBLIC hdf_core PROJ::proj Qt6::Gui)

function(hdf_deploy_projection target)
  target_link_libraries(${target} PRIVATE map_core)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/resources/proj"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HDF_PROJ_DATA}/proj.db" "$<TARGET_FILE_DIR:${target}>/resources/proj/proj.db"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/resources/proj/dependency-lock.json" "$<TARGET_FILE_DIR:${target}>/resources/proj/dependency-lock.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PROJ::proj>" "$<TARGET_FILE_DIR:${target}>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/resources/coastlines"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources/coastlines" "$<TARGET_FILE_DIR:${target}>/resources/coastlines"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/coastlines"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses/coastlines" "$<TARGET_FILE_DIR:${target}>/licenses/coastlines"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/proj"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HDF_PROJ_PREFIX}/share/proj/copyright" "$<TARGET_FILE_DIR:${target}>/licenses/proj/COPYING"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HDF_PROJ_PREFIX}/share/sqlite3/copyright" "$<TARGET_FILE_DIR:${target}>/licenses/proj/SQLITE-COPYRIGHT"
    VERBATIM)
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HDF_PROJ_PREFIX}/$<$<CONFIG:Debug>:debug/>bin/sqlite3.dll" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endif()
endfunction()

# Test-only platform plugin for deterministic headless runs.
function(hdf_projection_test_platform target)
  if(MSVC)
    get_filename_component(HDF_QT_PLUGIN_ROOT "${Qt6_DIR}/../../../plugins" ABSOLUTE)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HDF_QT_PLUGIN_ROOT}/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll" "$<TARGET_FILE_DIR:${target}>/platforms/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endif()
endfunction()

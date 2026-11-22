cmake_minimum_required(VERSION 3.16)
project(HDFStudio VERSION 0.12.12 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets Test)
qt_standard_project_setup()

if(UNIX AND NOT APPLE)
  set(LINUX TRUE)
endif()

#//////////////////////////
# include locations  
#//////////////////////////

include_directories(${CMAKE_SOURCE_DIR}/ext/hdf5-1.8.20/src)
include_directories(${CMAKE_SOURCE_DIR}/build/hdf5-1.8.20)
include_directories(${CMAKE_SOURCE_DIR}/ext/hdf5-1.8.20/test)

# Source tree layout (flat includes "nc_reader.hpp" keep working through these directories):
#   src/app     the main window, tree, grid, layer navigation (Qt Widgets)
#   src/core    reading, Properties and text export shared by the GUI, the tests and hdf_cli
#   src/views   image, vector and mesh windows
#   src/cli     hdf_cli
include_directories(${CMAKE_SOURCE_DIR}/src/app)
include_directories(${CMAKE_SOURCE_DIR}/src/core)
include_directories(${CMAKE_SOURCE_DIR}/src/views)

#//////////////////////////
# resources
#//////////////////////////

qt_add_resources(qrc_sources resources/hdf_explorer.qrc)
qt_add_resources(colormap_resources resources/colormaps.qrc)
list(APPEND qrc_sources ${colormap_resources})
if(WIN32)
  set(windows_resources resources/hdf_explorer.rc)
endif()
if(APPLE)
  set(MACOSX_BUNDLE_ICON_FILE sample.icns)
  set(app_icon_macos resources/sample.icns)
  set_source_files_properties(${app_icon_macos} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

set(src 
    src/app/hdf_explorer.hpp
    src/app/hdf_explorer.cpp
    src/app/layer_navigation.hpp
    src/app/layer_playback.hpp
    src/app/layer_playback.cpp
    src/app/slice_job_controller.hpp
    src/app/slice_job_controller.cpp
    src/app/section_dates.hpp
    src/app/section_dates.cpp
    src/app/workspace_sync.hpp
    src/app/workspace_sync.cpp
    src/app/visit.hpp
    src/app/visit.cpp
    src/app/iterate.hpp
    src/app/iterate.cpp
    src/views/vector_view.hpp
    src/views/analysis_table_view.hpp
    src/views/analysis_table_view.cpp
    src/views/vector_view.cpp
    src/views/mesh_view.hpp
    src/views/mesh_view.cpp
    src/views/raster_view.hpp
    src/views/raster_view.cpp
    src/views/raster_budget.hpp
    src/views/raster_budget.cpp
    src/views/residual_view.hpp
    src/views/residual_view.cpp
    src/views/texture_entropy_view.hpp
    src/views/texture_entropy_view.cpp
    src/views/hovmoller_view.hpp
    src/views/hovmoller_view.cpp
    src/views/section_canvas.hpp
    src/views/section_canvas.cpp
    src/views/section_snapshot.hpp
    src/views/compare_panel.hpp
    src/views/compare_panel.cpp
    src/views/histogram_view.hpp
    src/views/histogram_view.cpp
    src/views/profile_view.hpp
    src/views/profile_view.cpp
    src/views/color_bar.hpp
    src/views/color_bar.cpp
    src/views/color_map_picker.hpp
    src/views/color_map_picker.cpp
)

#//////////////////////////
# Qt libraries
#//////////////////////////

set(lib_dep 
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

#//////////////////////////
# HDF5 libraries 
#//////////////////////////

set(hdf5_dep)
if (MSVC)
    set(hdf_bin "${CMAKE_SOURCE_DIR}/build/hdf5-1.8.20/bin")
    list(APPEND hdf5_dep debug "${hdf_bin}/Debug/libhdf5_D.lib"
                         optimized "${hdf_bin}/Release/libhdf5.lib")
    set(zlib_bin "${CMAKE_SOURCE_DIR}/build/zlib")
    list(APPEND hdf5_dep debug "${zlib_bin}/Debug/zlibstaticd.lib"
                         optimized "${zlib_bin}/Release/zlibstatic.lib")
elseif(APPLE)
    link_directories(${CMAKE_SOURCE_DIR}/build/hdf5-1.8.20/bin)
    set(hdf5_dep libhdf5.a)
elseif(LINUX)
    link_directories(${CMAKE_SOURCE_DIR}/build/hdf5-1.8.20/bin)
    set(hdf5_dep libhdf5.a)
endif()

if (WIN32)
    list(APPEND hdf5_dep shlwapi advapi32 shell32 user32 kernel32)
endif()
set(lib_dep ${lib_dep} ${hdf5_dep})

add_executable(hdf_explorer ${src} ${qrc_sources} ${windows_resources} ${app_icon_macos})
set_target_properties(hdf_explorer PROPERTIES
    OUTPUT_NAME HDFStudio
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/output/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/output/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/build/output/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/build/output/MinSizeRel"
)
# Layers shared by the GUI, the tests and hdf_cli (no Qt Widgets below hdf_explorer):
#   nc_core  : netCDF-C reading (format probe, NetCDF-3 structure/values, image planes)
#   hdf_core : Properties summary and text export (HDF5, plus nc_core for NetCDF-3)
add_library(nc_core STATIC src/core/nc_reader.cpp src/core/nc_classic.cpp src/core/nc_slice.cpp src/core/slice_types.cpp)
target_link_libraries(nc_core PUBLIC Qt6::Core)
add_library(hdf_core STATIC src/core/properties_inspector.cpp src/core/text_export.cpp src/core/plane_reader.cpp src/core/hdf_slice.cpp src/core/curve_profile.cpp src/core/color_map.cpp src/core/image_metrics.cpp src/core/image_metrics.hpp src/core/texture_entropy.cpp src/core/texture_entropy.hpp)
target_link_libraries(hdf_core PUBLIC nc_core ${hdf5_dep})
target_sources(hdf_core PRIVATE src/core/slice_coordinates.cpp src/core/slice_io.cpp src/core/slice_worker.cpp)
add_executable(hdf_cli src/cli/hdf_cli.cpp ${colormap_resources})
target_link_libraries(hdf_cli PRIVATE hdf_core)
set_target_properties(hdf_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/output/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/output/Release"
)
target_link_libraries(hdf_explorer PRIVATE hdf_core)
add_executable(hdf_explorer_drop_tests
    tests/drop_targets_test.cpp
    ${src}
    ${qrc_sources}
    ${windows_resources}
)
set_target_properties(hdf_explorer_drop_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/output/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/output/Release"
)
target_compile_definitions(hdf_explorer_drop_tests PRIVATE HDF_EXPLORER_TEST_BUILD)
target_link_libraries(hdf_explorer_drop_tests PRIVATE hdf_core ${lib_dep} Qt6::Test)
# the tests also run hdf_cli to check that it prints what the GUI shows
add_dependencies(hdf_explorer_drop_tests hdf_cli)
if(MSVC)
  add_custom_command(TARGET hdf_explorer_drop_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/build/native-nc/Release/netcdf.dll" "$<TARGET_FILE_DIR:hdf_explorer_drop_tests>")
endif()
if(MSVC)
  target_include_directories(nc_core PUBLIC "${CMAKE_SOURCE_DIR}/ext/netcdf-c-4.9.3/include")
  target_link_libraries(nc_core PUBLIC "${CMAKE_SOURCE_DIR}/build/native-nc/Release/netcdf.lib")
  add_custom_command(TARGET hdf_explorer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/build/native-nc/Release/netcdf.dll" "$<TARGET_FILE_DIR:hdf_explorer>")
else()
  find_package(netCDF CONFIG REQUIRED)
  target_link_libraries(nc_core PUBLIC netCDF::netcdf)
endif()
target_link_libraries(hdf_explorer PRIVATE ${lib_dep})
set_target_properties(hdf_explorer PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
if(MSVC)
  foreach(target hdf_explorer hdf_explorer_drop_tests)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_PREFIX_PATH}/plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:${target}>/platforms/qwindows$<$<CONFIG:Debug>:d>.dll")
  endforeach()
  add_custom_command(TARGET hdf_explorer_drop_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:hdf_explorer_drop_tests>")
endif()
enable_testing()
add_executable(hovmoller_tests tests/hovmoller_test.cpp ${src} ${qrc_sources})
target_compile_definitions(hovmoller_tests PRIVATE HDF_EXPLORER_TEST_BUILD)
target_link_libraries(hovmoller_tests PRIVATE hdf_core ${lib_dep} Qt6::Test)
set_target_properties(hovmoller_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/output/Debug" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/output/Release")
add_dependencies(hovmoller_tests hdf_cli)
if(MSVC)
  add_custom_command(TARGET hovmoller_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:hovmoller_tests>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_PREFIX_PATH}/plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll" "$<TARGET_FILE_DIR:hovmoller_tests>/platforms/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:hovmoller_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/build/native-nc/Release/netcdf.dll" "$<TARGET_FILE_DIR:hovmoller_tests>")
endif()
add_test(NAME hovmoller_tests COMMAND hovmoller_tests)
add_test(NAME hdf_explorer_drop_tests COMMAND hdf_explorer_drop_tests)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT hdf_explorer)

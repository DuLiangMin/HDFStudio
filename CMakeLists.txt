cmake_minimum_required(VERSION 3.16)
project(HDFStudio VERSION 0.10.25 LANGUAGES C CXX)

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

#//////////////////////////
# resources
#//////////////////////////

qt_add_resources(qrc_sources hdf_explorer.qrc)
if(WIN32)
  set(windows_resources hdf_explorer.rc)
endif()
if(APPLE)
  set(MACOSX_BUNDLE_ICON_FILE sample.icns)
  set(app_icon_macos sample.icns)
  set_source_files_properties(${app_icon_macos} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

set(src 
    hdf_explorer.hpp
    hdf_explorer.cpp
    properties_inspector.hpp
    properties_inspector.cpp
    text_export.hpp
    text_export.cpp
    vector_view.hpp
    vector_view.cpp
    mesh_view.hpp
    mesh_view.cpp
    raster_view.cpp
    visit.hpp
    visit.cpp
    iterate.hpp
    iterate.cpp
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

if (MSVC)
    set(hdf_bin "${CMAKE_SOURCE_DIR}/build/hdf5-1.8.20/bin")
    list(APPEND lib_dep debug "${hdf_bin}/Debug/libhdf5_D.lib"
                        optimized "${hdf_bin}/Release/libhdf5.lib")
    set(zlib_bin "${CMAKE_SOURCE_DIR}/build/zlib")
    list(APPEND lib_dep debug "${zlib_bin}/Debug/zlibstaticd.lib"
                        optimized "${zlib_bin}/Release/zlibstatic.lib")
elseif(APPLE)
    link_directories(${CMAKE_SOURCE_DIR}/build/hdf5-1.8.20/bin)
    set(lib_dep ${lib_dep} libhdf5.a)
elseif(LINUX)
    link_directories(${CMAKE_SOURCE_DIR}/build/hdf5-1.8.20/bin)
    set(lib_dep ${lib_dep} libhdf5.a)
endif()

if (WIN32)
    set(lib_dep ${lib_dep} shlwapi advapi32 shell32 user32 kernel32)
endif()

add_executable(hdf_explorer ${src} ${qrc_sources} ${windows_resources} ${app_icon_macos})
set_target_properties(hdf_explorer PROPERTIES
    OUTPUT_NAME HDFStudio
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/output/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/output/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/build/output/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/build/output/MinSizeRel"
)
add_library(nc_core STATIC nc_reader.cpp)
target_link_libraries(nc_core PUBLIC Qt6::Core)
add_executable(hdf_cli nc_cli.cpp)
target_link_libraries(hdf_cli PRIVATE nc_core)
target_link_libraries(hdf_explorer PRIVATE nc_core)
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
target_link_libraries(hdf_explorer_drop_tests PRIVATE nc_core ${lib_dep} Qt6::Test)
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
add_test(NAME hdf_explorer_drop_tests COMMAND hdf_explorer_drop_tests)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT hdf_explorer)

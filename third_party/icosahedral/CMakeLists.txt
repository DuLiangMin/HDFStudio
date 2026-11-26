# Unmodified local 06 C17 kernels; source hashes and provenance are in source-manifest.json.
add_library(ico_native STATIC
  26NOV21_geometry.c 26NOV21_grid.c 26NOV21_mesh.c 26NOV21_legacy.c
  26NOV21_sparse.c 26NOV21_conservative.c 26NOV21_resolution.c 26NOV21_netcdf.c)
set_target_properties(ico_native PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(ico_native PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_include_directories(ico_native PRIVATE "${CMAKE_SOURCE_DIR}/ext/netcdf-c-4.9.3/include")
  target_compile_options(ico_native PRIVATE /utf-8)
  target_link_libraries(ico_native PUBLIC "${CMAKE_SOURCE_DIR}/build/native-nc/Release/netcdf.lib")
else()
  find_package(netCDF CONFIG REQUIRED)
  target_link_libraries(ico_native PUBLIC netCDF::netcdf m)
endif()

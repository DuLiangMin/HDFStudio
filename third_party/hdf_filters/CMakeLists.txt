# Fixed upstream sources; decode paths only. No plugins, network download or Python at build time.
add_library(hdf_filter_codecs STATIC
  bitshuffle/bitshuffle_core.c bitshuffle/iochain.c lz4/lz4.c
  zstd/lib/common/debug.c zstd/lib/common/entropy_common.c zstd/lib/common/error_private.c
  zstd/lib/common/fse_decompress.c zstd/lib/common/xxhash.c zstd/lib/common/zstd_common.c
  zstd/lib/decompress/huf_decompress.c zstd/lib/decompress/zstd_ddict.c
  zstd/lib/decompress/zstd_decompress.c zstd/lib/decompress/zstd_decompress_block.c)
set_target_properties(hdf_filter_codecs PROPERTIES AUTOMOC OFF AUTOUIC OFF C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_definitions(hdf_filter_codecs PRIVATE BSHUF_VERSION_MAJOR=0 BSHUF_VERSION_MINOR=5 BSHUF_VERSION_POINT=2 ZSTD_DISABLE_ASM=1)
target_include_directories(hdf_filter_codecs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/bitshuffle" "${CMAKE_CURRENT_SOURCE_DIR}/lz4" "${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib")
if(MSVC)
  target_compile_definitions(hdf_filter_codecs PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

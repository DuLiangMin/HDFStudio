#include "hdf_filters.hpp"
#include "bitshuffle_core.h"
#include "lz4.h"
#include "zstd.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {
constexpr H5Z_filter_t bitshuffle_id = 32008;
constexpr size_t chunk_limit = 256u * 1024u * 1024u;
thread_local QString last_decode_error;
uint64_t read_big_endian(const unsigned char *bytes, size_t count) {
  uint64_t value = 0;
  for(size_t index = 0; index < count; ++index) value = (value << 8) | bytes[index];
  return value;
}
size_t fail(const char *message) {
  last_decode_error = QString::fromLatin1(message);
  H5Epush1(__FILE__, __func__, __LINE__, H5E_PLINE, H5E_CANTFILTER, message);
  return 0;
}
}

size_t hdf_bitshuffle_decode(unsigned flags, size_t parameter_count,
    const unsigned parameters[], size_t input_bytes, size_t *buffer_bytes, void **buffer) {
  last_decode_error.clear();
  if(!(flags & H5Z_FLAG_REVERSE)) return fail("Bitshuffle is available for reading only");
  if(!parameters || parameter_count < 3 || !buffer_bytes || !buffer || !*buffer || input_bytes > *buffer_bytes)
    return fail("Invalid Bitshuffle parameters or input buffer");
  const size_t element_bytes = parameters[2];
  if(!element_bytes || element_bytes > chunk_limit / 8) return fail("Invalid Bitshuffle element size");
  const unsigned mode = parameter_count > 4 ? parameters[4] : 0;
  if(mode != 0 && mode != 2 && mode != 3) return fail("Unsupported Bitshuffle compression mode (expected none, LZ4 or Zstd)");
  size_t block_elements = parameter_count > 3 ? parameters[3] : 0;
  if(!block_elements) block_elements = bshuf_default_block_size(element_bytes);
  const auto *input = static_cast<const unsigned char*>(*buffer);
  size_t output_bytes = input_bytes, input_offset = 0;
  if(mode) {
    if(input_bytes < 12) return fail("Truncated Bitshuffle chunk header");
    const uint64_t declared = read_big_endian(input, 8);
    const uint64_t block_bytes = read_big_endian(input + 8, 4);
    if(declared > chunk_limit || !declared) return fail("Bitshuffle chunk exceeds the 256 MiB decode limit or is empty");
    if(!block_bytes || block_bytes % element_bytes) return fail("Invalid Bitshuffle block byte count");
    output_bytes = size_t(declared);
    block_elements = size_t(block_bytes / element_bytes);
    input_offset = 12;
  }
  if(!output_bytes || output_bytes > chunk_limit || output_bytes % element_bytes ||
      !block_elements || block_elements % 8 || block_elements > chunk_limit / element_bytes)
    return fail("Invalid Bitshuffle chunk size or block alignment");
  try {
    std::unique_ptr<void, decltype(&std::free)> output(std::malloc(output_bytes), &std::free);
    if(!output) return fail("Cannot allocate Bitshuffle decoded chunk");
    if(!mode) {
      if(bshuf_bitunshuffle(input, output.get(), output_bytes / element_bytes, element_bytes, block_elements) != int64_t(output_bytes))
        return fail("Bitshuffle unshuffle failed");
    } else {
      const size_t total_elements = output_bytes / element_bytes;
      size_t done_elements = 0;
      std::vector<unsigned char> shuffled(std::min(block_elements, total_elements) * element_bytes);
      while(total_elements - done_elements >= 8) {
        const size_t elements = std::min(block_elements, ((total_elements - done_elements) / 8) * 8);
        const size_t decoded_bytes = elements * element_bytes;
        if(input_bytes - input_offset < 4) return fail("Truncated Bitshuffle compressed block header");
        const uint64_t compressed_bytes = read_big_endian(input + input_offset, 4);
        input_offset += 4;
        if(!compressed_bytes || compressed_bytes > input_bytes - input_offset || compressed_bytes > chunk_limit)
          return fail("Truncated or invalid Bitshuffle compressed block");
        if(mode == 2) {
          const int decoded = LZ4_decompress_safe(reinterpret_cast<const char*>(input + input_offset),
              reinterpret_cast<char*>(shuffled.data()), int(compressed_bytes), int(decoded_bytes));
          if(decoded != int(decoded_bytes)) return fail("Bitshuffle LZ4 decoded length mismatch");
        } else {
          const size_t decoded = ZSTD_decompress(shuffled.data(), decoded_bytes, input + input_offset, size_t(compressed_bytes));
          if(ZSTD_isError(decoded) || decoded != decoded_bytes) return fail("Bitshuffle Zstd decompression failed or length mismatch");
        }
        auto *destination = static_cast<unsigned char*>(output.get()) + done_elements * element_bytes;
        if(bshuf_bitunshuffle(shuffled.data(), destination, elements, element_bytes, elements) != int64_t(decoded_bytes))
          return fail("Bitshuffle block unshuffle failed");
        input_offset += size_t(compressed_bytes);
        done_elements += elements;
      }
      const size_t tail_bytes = (total_elements - done_elements) * element_bytes;
      if(input_bytes - input_offset != tail_bytes) return fail("Bitshuffle tail length mismatch");
      if(tail_bytes) std::memcpy(static_cast<unsigned char*>(output.get()) + done_elements * element_bytes, input + input_offset, tail_bytes);
    }
    std::free(*buffer);
    *buffer = output.release();
    *buffer_bytes = output_bytes;
    return output_bytes;
  } catch(...) {
    return fail("Cannot allocate Bitshuffle working buffer");
  }
}

bool hdf_register_read_filters() {
  if(H5Zfilter_avail(bitshuffle_id) > 0) return true;
  const H5Z_class2_t filter = {H5Z_CLASS_T_VERS, bitshuffle_id, 0, 1,
      "Bitshuffle (none/LZ4/Zstd; HDFStudio read-only)", nullptr, nullptr, hdf_bitshuffle_decode};
  return H5Zregister(&filter) >= 0;
}

QString hdf_prepare_dataset(hid_t dataset) {
  last_decode_error.clear();
  if(!hdf_register_read_filters()) return QStringLiteral("Cannot register the built-in Bitshuffle decoder");
  const hid_t properties = H5Dget_create_plist(dataset);
  if(properties < 0) return QStringLiteral("Cannot inspect HDF5 dataset filters");
  QString error;
  const int count = H5Pget_nfilters(properties);
  if(count < 0) error = QStringLiteral("Cannot inspect HDF5 filter pipeline");
  for(int index = 0; error.isEmpty() && index < count; ++index) {
    char name[256] = {}; unsigned flags = 0, configuration = 0; size_t parameters = 0;
    const auto filter = H5Pget_filter2(properties, unsigned(index), &flags, &parameters, nullptr,
                                     sizeof(name), name, &configuration);
    if(filter < 0) { error = QStringLiteral("Cannot inspect HDF5 filter"); break; }
    if(H5Zfilter_avail(filter) <= 0 || H5Zget_filter_info(filter, &configuration) < 0 || !(configuration & H5Z_FILTER_CONFIG_DECODE_ENABLED))
      error = QString("HDF5 decompression filter %1 (%2) is unavailable. The file metadata is readable, but this dataset cannot be decoded.")
          .arg(filter).arg(QString::fromLocal8Bit(name));
  }
  H5Pclose(properties);
  return error;
}

QString hdf_filter_read_error(const QString &fallback) {
  return last_decode_error.isEmpty() ? fallback : fallback + ": " + last_decode_error;
}

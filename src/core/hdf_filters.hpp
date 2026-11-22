// Input: an HDF5 dataset/chunk. Output: native stored bytes or a precise read error.
// Decode-only filters shared by GUI, CLI and slice workers; no Python/plugin DLL dependency.
#pragma once
#include <hdf5.h>
#include <QString>

bool hdf_register_read_filters();
QString hdf_prepare_dataset(hid_t dataset);
QString hdf_filter_read_error(const QString &fallback);
size_t hdf_bitshuffle_decode(unsigned flags, size_t parameter_count,
    const unsigned parameters[], size_t input_bytes, size_t *buffer_bytes, void **buffer);

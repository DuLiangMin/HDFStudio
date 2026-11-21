// Local 4x4 gray-level entropy and numeric bilinear reconstruction; no Widgets or file I/O.
#pragma once
#include <QString>
#include <vector>

struct NcTextureEntropyPlane {
  bool valid = false;
  QString error;
  size_t rows = 0, cols = 0, block_rows = 0, block_cols = 0;
  size_t valid_count = 0, excluded_count = 0, valid_blocks = 0;
  double source_minimum = 0, source_maximum = 0;
  std::vector<double> block_entropy; // ceil(rows/4) x ceil(cols/4), bits, NaN = no valid samples.
  std::vector<double> values;        // rows x cols, interpolated bits; source gaps remain NaN.
};

NcTextureEntropyPlane nc_texture_entropy(const std::vector<double> &values, size_t rows, size_t cols);

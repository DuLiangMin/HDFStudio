// 256 shared gray levels, non-overlapping 4x4 Shannon entropy, then interpolate entropy values.
#include "texture_entropy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
struct AxisBlend {
  size_t first, second;
  double fraction;
};

double block_center(size_t block, size_t length) {
  const size_t start = block * 4;
  return double(start) + double(std::min(size_t(4), length - start) - 1) / 2;
}

AxisBlend axis_blend(size_t position, size_t length) {
  const size_t block = position / 4, count = (length + 3) / 4;
  const double center = block_center(block, length);
  size_t first = block, second = block;
  if(double(position) < center && block > 0) first = block - 1;
  if(double(position) > center && block + 1 < count) second = block + 1;
  const double fraction = first == second ? 0 :
    (double(position) - block_center(first, length)) /
    (block_center(second, length) - block_center(first, length));
  return {first, second, fraction};
}
}

NcTextureEntropyPlane nc_texture_entropy(const std::vector<double> &values, size_t rows, size_t cols) {
  NcTextureEntropyPlane result;
  if(!rows || !cols || rows > size_t(16 * 1024 * 1024) / cols || values.size() != rows * cols) {
    result.error = "Texture entropy requires a complete 2D plane, at most 16 megapixels";
    return result;
  }
  double minimum = INFINITY, maximum = -INFINITY;
  for(double value : values) {
    if(!std::isfinite(value)) { ++result.excluded_count; continue; }
    ++result.valid_count;
    minimum = std::min(minimum, value); maximum = std::max(maximum, value);
  }
  if(!result.valid_count) { result.error = "No finite unmasked samples for texture entropy"; return result; }
  result.rows = rows; result.cols = cols;
  result.block_rows = (rows + 3) / 4; result.block_cols = (cols + 3) / 4;
  result.source_minimum = minimum; result.source_maximum = maximum;
  const double missing = std::numeric_limits<double>::quiet_NaN();
  result.block_entropy.assign(result.block_rows * result.block_cols, missing);
  result.values.assign(values.size(), missing);
  const double span = maximum - minimum;
  const double scale = std::max(std::abs(minimum), std::abs(maximum));
  for(size_t block_row = 0; block_row < result.block_rows; ++block_row) {
    for(size_t block_col = 0; block_col < result.block_cols; ++block_col) {
      std::array<int, 16> levels{};
      size_t count = 0;
      for(size_t row = block_row * 4; row < std::min(rows, block_row * 4 + 4); ++row) {
        for(size_t col = block_col * 4; col < std::min(cols, block_col * 4 + 4); ++col) {
          const double value = values[row * cols + col];
          if(!std::isfinite(value)) continue;
          int level = 0;
          if(maximum > minimum) {
            const double fraction = std::isfinite(span) ? (value - minimum) / span :
              (value / scale - minimum / scale) / (maximum / scale - minimum / scale);
            level = std::clamp(int(fraction * 256), 0, 255);
          }
          levels[count++] = level;
        }
      }
      if(!count) continue;
      std::sort(levels.begin(), levels.begin() + count);
      double entropy = 0;
      for(size_t start = 0; start < count;) {
        size_t end = start + 1;
        while(end < count && levels[end] == levels[start]) ++end;
        const double probability = double(end - start) / double(count);
        entropy -= probability * std::log2(probability);
        start = end;
      }
      result.block_entropy[block_row * result.block_cols + block_col] = std::clamp(entropy, 0., 4.);
      ++result.valid_blocks;
    }
  }
  for(size_t row = 0; row < rows; ++row) {
    const auto vertical = axis_blend(row, rows);
    for(size_t col = 0; col < cols; ++col) {
      const size_t index = row * cols + col;
      if(!std::isfinite(values[index])) continue;
      const auto horizontal = axis_blend(col, cols);
      const size_t block_indices[] = {
        vertical.first * result.block_cols + horizontal.first,
        vertical.first * result.block_cols + horizontal.second,
        vertical.second * result.block_cols + horizontal.first,
        vertical.second * result.block_cols + horizontal.second};
      const double weights[] = {
        (1 - vertical.fraction) * (1 - horizontal.fraction),
        (1 - vertical.fraction) * horizontal.fraction,
        vertical.fraction * (1 - horizontal.fraction),
        vertical.fraction * horizontal.fraction};
      double weighted = 0, total_weight = 0;
      for(size_t neighbor = 0; neighbor < 4; ++neighbor) {
        const double entropy = result.block_entropy[block_indices[neighbor]];
        if(!std::isfinite(entropy) || weights[neighbor] <= 0) continue;
        weighted += entropy * weights[neighbor]; total_weight += weights[neighbor];
      }
      if(total_weight > 0) result.values[index] = std::clamp(weighted / total_weight, 0., 4.);
    }
  }
  result.valid = true;
  return result;
}

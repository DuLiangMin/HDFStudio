// Decoded-plane metrics shared by CLI and GUI; no Widgets or file I/O.
#pragma once
#include <QString>
#include <limits>
#include <vector>

struct NcGrayEntropy {
  bool valid = false;
  QString error;
  int bins = 256;
  size_t valid_count = 0, missing_count = 0;
  double minimum = 0, maximum = 0;
  double entropy_bits = 0, normalized_entropy = 0;
};

NcGrayEntropy nc_gray_entropy(const std::vector<double> &values, int bins = 256,
                             bool use_fixed_range = false,
                             double fixed_min = 0, double fixed_max = 0);

struct NcPairMetrics {
  bool valid = false, r_valid = false;
  QString error;
  size_t valid_pairs = 0, excluded_pairs = 0;
  double pearson_r = std::numeric_limits<double>::quiet_NaN();
  double rmse = 0, mae = 0, bias = 0;
};

NcPairMetrics nc_pair_metrics(const std::vector<double> &anchor, size_t anchor_rows, size_t anchor_cols,
                             const std::vector<double> &target, size_t target_rows, size_t target_cols);

struct NcResidualPlane {
  bool valid = false;
  QString error;
  size_t rows = 0, cols = 0, valid_pairs = 0, excluded_pairs = 0;
  std::vector<double> values;
  double absolute_max = 0;
};
NcResidualPlane nc_pair_residual(const std::vector<double> &anchor, size_t anchor_rows, size_t anchor_cols,
                                 const std::vector<double> &target, size_t target_rows, size_t target_cols);

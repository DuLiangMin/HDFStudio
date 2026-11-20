// One decoded plane of a variable, and the histogram of it as the Histogram window and `hdf_cli
// hist` compute it. Lives in hdf_core because a plain HDF5 file is read with the HDF5 API; a
// NetCDF file goes through NcReader::read_plane, so the image view, the histogram and hdf_cli all
// see the same numbers.
#pragma once
#include "nc_reader.hpp"
#include "hdf5.h"
#include <QString>
#include <vector>
#include <string>

// A 2-D plane of a variable as decoded doubles: _FillValue / missing_value become NaN, then
// decoded = stored x scale_factor + add_offset (CF 8.1). A one-dimensional variable is read as one
// row. `shape` is the variable shape; `start` holds one zero-based index per leading axis
// (shape.size() - 2 entries, at least 2 axes). reader receives "netCDF-C" or "HDF5".
struct NcPlaneResult {
  NcPlane plane;
  QString reader;
  QString error;
};
NcPlaneResult nc_read_plane_decoded(const QString &file, const std::string &path,
                                    const std::vector<hsize_t> &shape,
                                    const std::vector<size_t> &start);

// Histogram of one decoded plane. The kept values are drawn at random from the whole plane: exactly
// round(finite * keep_fraction) of them, so "1 in 20" always means a twentieth of the values.
struct NcHistogram {
  bool valid = false;
  QString error;                 // why the histogram is empty (no finite values, bad fraction)
  int bins = 0;                  // number of bins actually used
  std::vector<double> edges;     // bins + 1 bounds, decoded values, edges[0] = min, back() = max
  std::vector<size_t> counts;    // values per bin; sum = sampled
  size_t total = 0;              // finite values in the whole plane
  size_t missing = 0;            // NaN values (file missing value) in the whole plane
  size_t sampled = 0;            // values drawn at random; the bars are built from these
  // Range and moments of the kept sample; choose 1 in 1 for the exact values of the whole plane.
  double minimum = 0, maximum = 0, mean = 0, stddev = 0;
  double keep_fraction = 1.0;    // 1 / every_nth_value
  int every_nth = 1;             // the same fraction written as "1 in N"
  quint32 seed = 1;
};

// keep_fraction in (0, 1]: 0.05 keeps one value in twenty. bins >= 1. The same seed always keeps
// the same values, so two windows with the same settings show the same bars; another seed is
// another random sample of the same size.
NcHistogram nc_histogram(const std::vector<double> &decoded_values, double keep_fraction,
                         int bins, quint32 seed = 1);

// Text histogram for hdf_cli hist: one line per bin with a bar of at most bar_width characters.
QStringList nc_histogram_lines(const NcHistogram &histogram, int bar_width = 40);

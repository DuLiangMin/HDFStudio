// Plane decoding and histogram binning shared by the image view, the histogram window and hdf_cli.
#include "plane_reader.hpp"
#include "nc_classic.hpp"
#include <QFileInfo>
#include <QObject>
#include <climits>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct Handle {
  hid_t id;
  herr_t (*close)(hid_t);
  ~Handle() { if(id >= 0) close(id); }
};

bool scalar_attr(hid_t dataset, const char *name, double &out) {
  if(H5Aexists(dataset, name) <= 0) return false;
  Handle attr{H5Aopen(dataset, name, H5P_DEFAULT), H5Aclose};
  if(attr.id < 0) return false;
  Handle space{H5Aget_space(attr.id), H5Sclose};
  return space.id >= 0 && H5Sget_simple_extent_npoints(space.id) == 1 &&
         H5Aread(attr.id, H5T_NATIVE_DOUBLE, &out) >= 0;
}

// xorshift64*: a small deterministic generator, so the same seed keeps the same values and a test
// can compare two runs exactly.
class Sampler {
public:
  explicit Sampler(quint32 seed) : state(0x9E3779B97F4A7C15ULL ^ (quint64(seed) * 0x2545F4914F6CDD1DULL)) {}
  // uniform in [0, 1)
  double next() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    const quint64 value = state * 0x2545F4914F6CDD1DULL;
    return double(value >> 11) * (1.0 / 9007199254740992.0);   // 2^53
  }
private:
  quint64 state;
};

QString number(double value) { return QString::number(value, 'g', 15); }

}   // namespace

NcPlaneResult nc_read_plane_decoded(const QString &file, const std::string &path,
                                    const std::vector<hsize_t> &shape,
                                    const std::vector<size_t> &start) {
  NcPlaneResult result;
  if(shape.size() < 2) { result.error = QObject::tr("A histogram needs at least a two-dimensional variable"); return result; }
  const hsize_t rows = shape[shape.size() - 2], cols = shape.back();
  if(rows == 0 || cols == 0) { result.error = QObject::tr("This variable has no values yet."); return result; }
  if(rows > quint64(INT_MAX) || cols > quint64(INT_MAX) || rows * cols > 16ull * 1024 * 1024) {
    result.error = QObject::tr("Plane exceeds the 16 megapixel preview limit.");
    return result;
  }

  const bool may_be_netcdf = is_classic_netcdf_file(file) ||
      file.endsWith(".nc", Qt::CaseInsensitive) || file.endsWith(".nc4", Qt::CaseInsensitive);
  if(may_be_netcdf) {
    NcReader reader(file);
    int group = -1, variable = -1;
    if(reader.locate(path, group, variable)) {
      NcPlane plane;
      const QString error = reader.read_plane(path, start, plane);
      if(error.isEmpty() && plane.rows == rows && plane.cols == cols) {
        result.plane = std::move(plane);
        result.reader = "netCDF-C";
        return result;
      }
      if(error.isEmpty()) result.error = QObject::tr("The plane shape does not match the variable");
      else result.error = error;
      return result;
    }
  }

  Handle fid{H5Fopen(nc_path_bytes(file).constData(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose};
  if(fid.id < 0) { result.error = QObject::tr("HDF5 cannot open the file"); return result; }
  Handle dataset{H5Dopen2(fid.id, path.c_str(), H5P_DEFAULT), H5Dclose};
  if(dataset.id < 0) { result.error = QObject::tr("Variable not found"); return result; }
  Handle space{H5Dget_space(dataset.id), H5Sclose};
  if(space.id < 0 || H5Sget_simple_extent_ndims(space.id) != int(shape.size())) {
    result.error = QObject::tr("Cannot read the variable shape");
    return result;
  }
  std::vector<hsize_t> hstart(shape.size(), 0), count(shape.size(), 1);
  for(size_t axis = 0; axis < shape.size() - 2 && axis < start.size(); ++axis) hstart[axis] = start[axis];
  count[shape.size() - 2] = rows;
  count.back() = cols;
  if(H5Sselect_hyperslab(space.id, H5S_SELECT_SET, hstart.data(), nullptr, count.data(), nullptr) < 0) {
    result.error = QObject::tr("Cannot select the plane");
    return result;
  }
  const hsize_t plane_shape[2] = {rows, cols};
  Handle memory{H5Screate_simple(2, plane_shape, nullptr), H5Sclose};
  if(memory.id < 0) { result.error = QObject::tr("Cannot allocate the plane"); return result; }

  std::vector<double> values(size_t(rows * cols), NAN);
  if(H5Dread(dataset.id, H5T_NATIVE_DOUBLE, memory.id, space.id, H5P_DEFAULT, values.data()) < 0) {
    result.error = QObject::tr("Cannot read the plane");
    return result;
  }
  double fill = NAN, missing = NAN, scale = 1, offset = 0;
  scalar_attr(dataset.id, "_FillValue", fill);
  scalar_attr(dataset.id, "missing_value", missing);
  scalar_attr(dataset.id, "scale_factor", scale);
  scalar_attr(dataset.id, "add_offset", offset);
  for(double &value : values)
    value = (value == fill || value == missing) ? NAN : value * scale + offset;

  result.plane.rows = size_t(rows);
  result.plane.cols = size_t(cols);
  result.plane.values = std::move(values);
  result.reader = "HDF5";
  return result;
}

NcHistogram nc_histogram(const std::vector<double> &decoded_values, double keep_fraction,
                         int bins, quint32 seed) {
  NcHistogram result;
  result.keep_fraction = keep_fraction;
  result.seed = seed;
  if(!(keep_fraction > 0.0) || keep_fraction > 1.0) {
    result.error = QObject::tr("The sampling fraction must be greater than 0 and at most 1");
    return result;
  }
  if(bins < 1) {
    result.error = QObject::tr("The number of bins must be at least 1");
    return result;
  }

  // Count first (no arithmetic beyond a compare), so the sample size is exact: k = round(finite x
  // keep_fraction). Then draw exactly k values uniformly without replacement -- each value is kept
  // with probability "still needed / still to come", which keeps the sample uniform over the plane
  // and makes "1 in 20" a twentieth of the values rather than a grid of every twentieth position.
  for(const double value : decoded_values) {
    if(std::isfinite(value)) ++result.total;
    else ++result.missing;
  }
  if(result.total == 0) {
    result.error = QObject::tr("No finite value in this plane");
    return result;
  }
  const size_t want = keep_fraction >= 1.0
      ? result.total
      : std::clamp<size_t>(size_t(std::llround(double(result.total) * keep_fraction)), 1, result.total);
  Sampler sampler(seed);
  std::vector<double> kept;
  kept.reserve(want);
  size_t remaining = want;      // values still to draw
  size_t left = result.total;   // finite values still to come (this one included)
  for(const double value : decoded_values) {
    if(!std::isfinite(value)) continue;
    // keep this one when the draw falls under the proportion still needed
    if(remaining > 0 && (remaining == left || sampler.next() * double(left) < double(remaining))) {
      kept.push_back(value);
      --remaining;
    }
    --left;
  }

  // range and moments of the kept sample, in one pass
  double low = INFINITY, high = -INFINITY, mean = 0, spread = 0;   // Welford for the variance
  for(size_t i = 0; i < kept.size(); ++i) {
    const double value = kept[i];
    low = std::min(low, value);
    high = std::max(high, value);
    const double delta = value - mean;
    mean += delta / double(i + 1);
    spread += delta * (value - mean);
  }
  result.sampled = kept.size();
  result.every_nth = int(std::lround(1.0 / keep_fraction));
  result.minimum = low;
  result.maximum = high;
  result.mean = mean;
  result.stddev = kept.size() > 1 ? std::sqrt(spread / double(kept.size() - 1)) : 0.0;

  // Bins over [min, max]; a constant plane gets a single bin.
  const bool constant = !(high > low);
  result.bins = constant ? 1 : bins;
  result.edges.resize(size_t(result.bins) + 1);
  result.counts.assign(size_t(result.bins), 0);
  if(constant) {
    // a readable width for a single bin: one unit, or the value's own magnitude when it is huge
    const double width = std::fabs(low) > 1.0 ? std::fabs(low) / 1000.0 : 1.0;
    result.edges[0] = low - width / 2.0;
    result.edges[1] = low + width / 2.0;
    result.counts[0] = kept.size();
  } else {
    const double step = (high - low) / double(result.bins);
    for(int bin = 0; bin <= result.bins; ++bin) result.edges[size_t(bin)] = low + step * double(bin);
    result.edges[size_t(result.bins)] = high;   // no accumulated rounding on the last bound
    for(const double value : kept) {
      int bin = int(std::floor((value - low) / step));
      bin = std::clamp(bin, 0, result.bins - 1);   // the maximum value belongs to the last bin
      ++result.counts[size_t(bin)];
    }
  }
  result.valid = true;
  return result;
}

QStringList nc_histogram_lines(const NcHistogram &histogram, int bar_width) {
  QStringList lines;
  if(!histogram.valid) {
    lines << QString("histogram: %1").arg(histogram.error.isEmpty()
        ? QObject::tr("no values") : histogram.error);
    return lines;
  }
  lines << QString("values: %1 finite, %2 missing   sampled %3 at random (1 in %4, seed %5)")
               .arg(histogram.total).arg(histogram.missing).arg(histogram.sampled)
               .arg(histogram.every_nth).arg(histogram.seed);
  lines << QString("range: [%1, %2]   mean %3   stddev %4")
               .arg(number(histogram.minimum), number(histogram.maximum),
                    number(histogram.mean), number(histogram.stddev));
  lines << QString("bins: %1   width %2").arg(histogram.bins)
               .arg(number((histogram.edges.back() - histogram.edges.front()) / double(histogram.bins)));
  const size_t peak = *std::max_element(histogram.counts.begin(), histogram.counts.end());
  const int width = std::clamp(bar_width, 1, 200);
  for(int bin = 0; bin < histogram.bins; ++bin) {
    const size_t count = histogram.counts[size_t(bin)];
    const int bar = peak == 0 ? 0 : int(double(count) / double(peak) * double(width) + 0.5);
    lines << QString("%1  [%2, %3)  %4  %5")
                 .arg(bin, 3).arg(number(histogram.edges[size_t(bin)]),
                                  number(histogram.edges[size_t(bin) + 1]))
                 .arg(count, 8).arg(QString(bar, '#'));
  }
  return lines;
}

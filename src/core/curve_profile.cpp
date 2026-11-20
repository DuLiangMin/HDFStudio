#include "curve_profile.hpp"
#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

double distance_between(const QPointF &a, const QPointF &b) {
  return std::hypot(b.x() - a.x(), b.y() - a.y());
}

QPointF catmull_rom(const QPointF &p0, const QPointF &p1, const QPointF &p2, const QPointF &p3, double t) {
  const double t2 = t * t;
  const double t3 = t2 * t;
  const double x = 0.5 * ((2.0 * p1.x()) +
                          (-p0.x() + p2.x()) * t +
                          (2.0 * p0.x() - 5.0 * p1.x() + 4.0 * p2.x() - p3.x()) * t2 +
                          (-p0.x() + 3.0 * p1.x() - 3.0 * p2.x() + p3.x()) * t3);
  const double y = 0.5 * ((2.0 * p1.y()) +
                          (-p0.y() + p2.y()) * t +
                          (2.0 * p0.y() - 5.0 * p1.y() + 4.0 * p2.y() - p3.y()) * t2 +
                          (-p0.y() + 3.0 * p1.y() - 3.0 * p2.y() + p3.y()) * t3);
  return QPointF(x, y);
}

std::vector<QPointF> resample_by_arclength(const std::vector<QPointF> &dense, double step_pixels) {
  if(dense.size() < 2) return dense;
  const double step = std::max(0.05, step_pixels);
  std::vector<double> cumulative(dense.size(), 0.0);
  for(size_t i = 1; i < dense.size(); ++i)
    cumulative[i] = cumulative[i - 1] + distance_between(dense[i - 1], dense[i]);
  const double total = cumulative.back();
  if(total <= 0.0) return {dense.front()};

  std::vector<QPointF> sampled;
  sampled.push_back(dense.front());
  size_t seg = 0;
  for(double d = step; d < total; d += step) {
    while(seg + 1 < cumulative.size() && cumulative[seg + 1] < d) ++seg;
    if(seg + 1 >= cumulative.size()) break;
    const double seg_len = cumulative[seg + 1] - cumulative[seg];
    const double alpha = seg_len > 0.0 ? (d - cumulative[seg]) / seg_len : 0.0;
    const QPointF &a = dense[seg];
    const QPointF &b = dense[seg + 1];
    sampled.emplace_back(a.x() + (b.x() - a.x()) * alpha, a.y() + (b.y() - a.y()) * alpha);
  }
  if(distance_between(sampled.back(), dense.back()) > 1e-6)
    sampled.push_back(dense.back());
  return sampled;
}

double sample_plane(const std::vector<double> &values, size_t rows, size_t cols,
                    double x, double y, ProfileInterpMode interp) {
  if(x < 0.0 || y < 0.0 || x > double(cols - 1) || y > double(rows - 1))
    return std::numeric_limits<double>::quiet_NaN();
  auto at = [&values, cols](size_t r, size_t c) { return values[r * cols + c]; };
  if(interp == ProfileInterpMode::Nearest) {
    const size_t r = std::clamp<size_t>(size_t(std::llround(y)), 0, rows - 1);
    const size_t c = std::clamp<size_t>(size_t(std::llround(x)), 0, cols - 1);
    return at(r, c);
  }
  const size_t c0 = std::min(size_t(std::floor(x)), cols - 1);
  const size_t r0 = std::min(size_t(std::floor(y)), rows - 1);
  const size_t c1 = std::min(c0 + 1, cols - 1);
  const size_t r1 = std::min(r0 + 1, rows - 1);
  const double tx = x - double(c0);
  const double ty = y - double(r0);
  const double v00 = at(r0, c0);
  const double v10 = at(r0, c1);
  const double v01 = at(r1, c0);
  const double v11 = at(r1, c1);
  if(std::isfinite(v00) && std::isfinite(v10) && std::isfinite(v01) && std::isfinite(v11)) {
    const double top = v00 * (1.0 - tx) + v10 * tx;
    const double bottom = v01 * (1.0 - tx) + v11 * tx;
    return top * (1.0 - ty) + bottom * ty;
  }
  const size_t nr = std::clamp<size_t>(size_t(std::llround(y)), 0, rows - 1);
  const size_t nc = std::clamp<size_t>(size_t(std::llround(x)), 0, cols - 1);
  return at(nr, nc);
}

}   // namespace

std::vector<QPointF> curve_sample_path(const std::vector<QPointF> &control_points,
                                       CurvePathMode mode,
                                       double step_pixels) {
  if(control_points.size() < 2) return {};
  if(mode == CurvePathMode::Polyline || control_points.size() == 2)
    return resample_by_arclength(control_points, step_pixels);

  std::vector<QPointF> dense;
  const size_t n = control_points.size();
  dense.reserve(n * 24);
  for(size_t i = 0; i + 1 < n; ++i) {
    const QPointF p0 = i == 0 ? control_points[0] : control_points[i - 1];
    const QPointF p1 = control_points[i];
    const QPointF p2 = control_points[i + 1];
    const QPointF p3 = (i + 2 < n) ? control_points[i + 2] : control_points[n - 1];
    const double chord = distance_between(p1, p2);
    const int substeps = std::clamp(int(std::ceil(chord / std::max(0.1, step_pixels) * 2.0)), 8, 256);
    for(int s = (i == 0 ? 0 : 1); s <= substeps; ++s) {
      const double t = double(s) / double(substeps);
      dense.push_back(catmull_rom(p0, p1, p2, p3, t));
    }
  }
  return resample_by_arclength(dense, step_pixels);
}

CurveProfileResult curve_extract_profile(const std::vector<double> &decoded_values,
                                         size_t rows, size_t cols,
                                         const std::vector<QPointF> &sampled_points,
                                         ProfileInterpMode interp) {
  CurveProfileResult result;
  if(rows == 0 || cols == 0 || decoded_values.size() != rows * cols) {
    result.error = QObject::tr("Invalid plane dimensions for profile extraction.");
    return result;
  }
  if(sampled_points.size() < 2) {
    result.error = QObject::tr("At least two points are required for a profile.");
    return result;
  }

  result.points.reserve(sampled_points.size());
  double dist = 0.0;
  double low = std::numeric_limits<double>::infinity();
  double high = -std::numeric_limits<double>::infinity();
  double sum = 0.0;
  for(size_t i = 0; i < sampled_points.size(); ++i) {
    if(i > 0) dist += distance_between(sampled_points[i - 1], sampled_points[i]);
    const double v = sample_plane(decoded_values, rows, cols, sampled_points[i].x(), sampled_points[i].y(), interp);
    result.points.push_back({dist, sampled_points[i].x(), sampled_points[i].y(), v});
    if(std::isfinite(v)) {
      ++result.finite_count;
      low = std::min(low, v);
      high = std::max(high, v);
      sum += v;
    } else {
      ++result.missing_count;
    }
  }
  result.total_length = dist;
  if(result.finite_count > 0) {
    result.minimum = low;
    result.maximum = high;
    result.mean = sum / double(result.finite_count);
  }
  result.valid = true;
  return result;
}

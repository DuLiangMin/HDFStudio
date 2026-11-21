// Full-plane gray-level Shannon entropy; nonfinite values are excluded.
#include "image_metrics.hpp"
#include <algorithm>
#include <cmath>

NcGrayEntropy nc_gray_entropy(const std::vector<double> &values, int bins,
                             bool use_fixed_range, double fixed_min, double fixed_max) {
  NcGrayEntropy result;
  result.bins = bins;
  if(bins < 2 || bins > 65536) {
    result.error = "The number of bins must be between 2 and 65536";
    return result;
  }
  if(use_fixed_range && (!std::isfinite(fixed_min) || !std::isfinite(fixed_max) || fixed_max <= fixed_min)) {
    result.error = "A fixed entropy range must be finite and increasing";
    return result;
  }
  double minimum = INFINITY, maximum = -INFINITY;
  for(double value : values) {
    if(!std::isfinite(value)) { ++result.missing_count; continue; }
    ++result.valid_count;
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
  }
  if(!result.valid_count) { result.error = "No finite values"; return result; }
  result.minimum = use_fixed_range ? fixed_min : minimum;
  result.maximum = use_fixed_range ? fixed_max : maximum;
  result.valid = true;
  if(minimum == maximum) return result;
  std::vector<size_t> counts(size_t(bins), 0);
  // Scale first to avoid overflow in high-low for finite opposite-sign extremes.
  const double scale = std::max(std::abs(result.minimum), std::abs(result.maximum));
  const double low = result.minimum / scale, high = result.maximum / scale;
  const double span=result.maximum-result.minimum;
  for(double value : values) {
    if(!std::isfinite(value)) continue;
    int index = 0;
    if(value >= result.maximum) index = bins - 1;
    else if(value > result.minimum) {
      const double fraction = std::isfinite(span) ? (value-result.minimum)/span : (value/scale-low)/(high-low);
      index = std::clamp(int(fraction * bins), 0, bins - 1);
    }
    ++counts[size_t(index)];
  }
  for(size_t count : counts) {
    if(!count) continue;
    const double probability = double(count) / double(result.valid_count);
    result.entropy_bits -= probability * std::log2(probability);
  }
  result.entropy_bits = std::clamp(result.entropy_bits, 0.0, std::log2(double(bins)));
  result.normalized_entropy = result.entropy_bits / std::log2(double(bins));
  return result;
}

namespace {
// Neumaier compensation: deterministic sums without dependence on extended precision.
struct StableSum {
  double sum = 0, correction = 0;
  void add(double value) {
    const double next = sum + value;
    correction += std::abs(sum) >= std::abs(value) ? (sum-next)+value : (value-next)+sum;
    sum = next;
  }
  double value() const { return sum + correction; }
};
}

NcResidualPlane nc_pair_residual(const std::vector<double> &anchor, size_t anchor_rows, size_t anchor_cols,
                                const std::vector<double> &target, size_t target_rows, size_t target_cols) {
  NcResidualPlane result;
  if(anchor_rows!=target_rows || anchor_cols!=target_cols || anchor.size()!=target.size() ||
     !anchor_rows || !anchor_cols || anchor_rows>std::numeric_limits<size_t>::max()/anchor_cols ||
     anchor_rows*anchor_cols!=anchor.size()) {
    result.error="Plane shapes do not match";return result;
  }
  result.rows=anchor_rows;result.cols=anchor_cols;
  result.values.resize(anchor.size(),std::numeric_limits<double>::quiet_NaN());
  for(size_t i=0;i<anchor.size();++i) {
    if(!std::isfinite(anchor[i]) || !std::isfinite(target[i])) {++result.excluded_pairs;continue;}
    const double value=target[i]-anchor[i];
    if(!std::isfinite(value)) {result.error="Residual exceeds the finite double range";result.values.clear();return result;}
    result.values[i]=value;++result.valid_pairs;
    result.absolute_max=std::max(result.absolute_max,std::abs(value));
  }
  result.valid=result.valid_pairs>0;
  if(!result.valid) result.error="No finite paired values";
  return result;
}

NcPairMetrics nc_pair_metrics(const std::vector<double> &anchor, size_t anchor_rows, size_t anchor_cols,
                             const std::vector<double> &target, size_t target_rows, size_t target_cols) {
  NcPairMetrics result;
  if(anchor_rows != target_rows || anchor_cols != target_cols || anchor.size() != target.size() ||
     !anchor_rows || !anchor_cols || anchor_rows > std::numeric_limits<size_t>::max()/anchor_cols ||
     anchor_rows*anchor_cols != anchor.size()) {
    result.error = "Plane shapes do not match"; return result;
  }
  double a_min = INFINITY, a_max = -INFINITY, b_min = INFINITY, b_max = -INFINITY;
  double maximum_difference=0;
  bool difference_overflow=false;
  for(size_t i=0; i<anchor.size(); ++i) {
    const double a=anchor[i], b=target[i];
    if(!std::isfinite(a) || !std::isfinite(b)) { ++result.excluded_pairs; continue; }
    ++result.valid_pairs;
    a_min=std::min(a_min,a); a_max=std::max(a_max,a);
    b_min=std::min(b_min,b); b_max=std::max(b_max,b);
    const double difference=b-a;
    if(std::isfinite(difference)) maximum_difference=std::max(maximum_difference,std::abs(difference));
    else difference_overflow=true;
  }
  if(!result.valid_pairs) { result.error="No finite paired values"; return result; }
  const double a_center=a_min/2+a_max/2, b_center=b_min/2+b_max/2;
  const double a_scale=std::max(std::abs(a_min-a_center),std::abs(a_max-a_center));
  const double b_scale=std::max(std::abs(b_min-b_center),std::abs(b_max-b_center));
  const double error_scale=difference_overflow
    ? std::max({std::abs(a_min),std::abs(a_max),std::abs(b_min),std::abs(b_max)}) : maximum_difference;
  StableSum a_sum,b_sum,error_sum,absolute_sum,square_sum;
  for(size_t i=0; i<anchor.size(); ++i) {
    const double a=anchor[i], b=target[i];
    if(!std::isfinite(a) || !std::isfinite(b)) continue;
    if(a_scale>0) a_sum.add((a-a_center)/a_scale);
    if(b_scale>0) b_sum.add((b-b_center)/b_scale);
    // Subtract first when possible, retaining small errors between large values.
    const double raw_diff=b-a;
    const double diff=error_scale==0 ? 0 : std::isfinite(raw_diff) ? raw_diff/error_scale : b/error_scale-a/error_scale;
    error_sum.add(diff); absolute_sum.add(std::abs(diff)); square_sum.add(diff*diff);
  }
  const double count=double(result.valid_pairs);
  result.valid=true;
  result.bias=(error_sum.value()/count)*error_scale;
  result.mae=(absolute_sum.value()/count)*error_scale;
  result.rmse=std::sqrt(square_sum.value()/count)*error_scale;
  if(result.valid_pairs<2 || a_scale==0 || b_scale==0) return result;
  const double a_mean=a_sum.value()/count, b_mean=b_sum.value()/count;
  StableSum variance_a,variance_b,covariance;
  for(size_t i=0; i<anchor.size(); ++i) {
    if(!std::isfinite(anchor[i]) || !std::isfinite(target[i])) continue;
    const double a=(anchor[i]-a_center)/a_scale-a_mean;
    const double b=(target[i]-b_center)/b_scale-b_mean;
    variance_a.add(a*a); variance_b.add(b*b); covariance.add(a*b);
  }
  if(variance_a.value()>0 && variance_b.value()>0) {
    result.pearson_r=std::clamp(covariance.value()/std::sqrt(variance_a.value())/std::sqrt(variance_b.value()),-1.0,1.0);
    result.r_valid=std::isfinite(result.pearson_r);
  }
  return result;
}

#pragma once
#include <QPointF>
#include <QString>
#include <vector>

enum class CurvePathMode {
  Polyline,      // 点到点折线
  SmoothSpline   // Catmull-Rom 样条（过所有控制点，数学等价于 GDI+ DrawCurve）
};

enum class ProfileInterpMode {
  Bilinear,      // 双线性插值（四角任一为 NaN 时退化为最近邻）
  Nearest        // 最近邻
};

struct ProfilePoint {
  double distance = 0.0;   // 沿曲线累计像素距离（起点为 0）
  double x = 0.0;          // 列坐标 col（浮点）
  double y = 0.0;          // 行坐标 row（浮点）
  double value = 0.0;      // 解码值（越界或缺失为 NaN）
};

struct CurveProfileResult {
  bool valid = false;
  QString error;
  std::vector<ProfilePoint> points;
  size_t finite_count = 0;
  size_t missing_count = 0;
  double minimum = 0.0;
  double maximum = 0.0;
  double mean = 0.0;
  double total_length = 0.0;
};

// 1) 由控制点生成沿曲线等弧长采样的 (x, y) 点列；control_points.size() < 2 返回空
std::vector<QPointF> curve_sample_path(const std::vector<QPointF> &control_points,
                                       CurvePathMode mode,
                                       double step_pixels = 0.5);

// 2) 在二维解码平面 values[row * cols + col] 上沿 sampled_points 提取剖面
CurveProfileResult curve_extract_profile(const std::vector<double> &decoded_values,
                                         size_t rows, size_t cols,
                                         const std::vector<QPointF> &sampled_points,
                                         ProfileInterpMode interp = ProfileInterpMode::Bilinear);

// 任意两轴切片公共接口；维度序号均相对于变量，索引均为0-based。
#pragma once
#include <QString>
#include <QStringList>
#include <vector>
#include <cstddef>

struct NcSliceSpec {
  size_t row_axis = 0, col_axis = 1;
  std::vector<size_t> fixed_indices;
};
enum class SliceAxisRole { Unknown, Time, Latitude, Longitude, Other };
struct SliceAxisDescriptor {
  size_t dimension_index = 0, length = 0;
  QString dimension_name, coordinate_path, units, calendar, standard_name;
  SliceAxisRole role = SliceAxisRole::Unknown;
  QString association = "none", diagnostic, calendar_source = "none";
  bool date_supported = false;
};
QString slice_validate(const std::vector<size_t> &shape, const NcSliceSpec &spec);
void slice_orient(std::vector<double> &values, size_t rows, size_t cols, bool transpose);

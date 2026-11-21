// Read-only decoded, currently masked plane. Values are row-major shape(rows,cols).
#pragma once
#include <QString>
#include <QtGlobal>
#include <memory>
#include <vector>

struct PlaneSnapshot {
  QString file_path, variable_path;
  std::vector<size_t> leading_indices;
  size_t rows = 0, cols = 0;
  QString units = "unspecified";
  std::shared_ptr<const std::vector<double>> values;
  std::shared_ptr<void> storage_lease; // Budget remains reserved while consumers retain the plane.
  bool out_of_range = false;
  quint64 revision = 0;
};

#pragma once
#include "slice_io.hpp"
#include <memory>
struct SliceWorkerData {
  SliceInfo info;
  NcSliceSpec spec;
  NcPlane plane;
  std::map<size_t,std::vector<double>> coordinates;
  QJsonObject metadata;
  QString error;
};
int slice_worker(const QString &request_path);
SliceWorkerData slice_load_result(const QString &directory,quint64 request_id);

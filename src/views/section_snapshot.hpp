#pragma once
#include "slice_worker.hpp"
#include "color_map.hpp"
#include <QImage>
struct SectionSnapshot {
  QString file,variable;
  SliceInfo info;
  NcSliceSpec spec;
  std::shared_ptr<const std::map<size_t,std::vector<double>>> coordinates;
  std::shared_ptr<const std::vector<double>> decoded,effective;
  std::shared_ptr<void> lease;
  HdfColorMap::Palette palette;
  HdfColorMap::Mapping mapping;
  QImage image;
  QJsonObject metadata;
  quint64 request_id=0,render_revision=0;
  size_t missing=0,masked=0,valid=0;
  bool flip_h=false,flip_v=false,has_range=false;
};

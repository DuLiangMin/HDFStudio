// 变量描述与坐标关联；不把相同长度当作同一维度。
#pragma once
#include "plane_reader.hpp"
#include <map>
struct SliceInfo {
  std::vector<hsize_t> shape;
  std::vector<SliceAxisDescriptor> axes;
  QString units, reader, error, error_code;
  QString storage_type;
  bool netcdf_identity=false;
  quint64 chunk_bytes=0;
  QStringList diagnostics;
  QString source_kind = "hdf", decode_profile = "DecodeProfileV1";
  quint64 source_working_bytes = 0;
  qint64 catalog_scan_ms = 0;
};
SliceInfo slice_inspect(const QString &file,const std::string &path);
std::vector<double> slice_read_coordinate(const QString &file,const SliceAxisDescriptor &axis,QString &error,size_t index=size_t(-1));
QString slice_nearest(const std::vector<double> &values,double requested,size_t &index);

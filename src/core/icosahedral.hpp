// Native 06 C kernels + explicit point-sampling adapter. No Qt Widgets or file reads.
#pragma once
#include "geo_grid.hpp"
#include "plane_snapshot.hpp"
#include "ico_atlas.hpp"
#include <array>
#include <functional>

namespace Icosahedral {
using Vec = std::array<double,3>;
using Face = std::array<int32_t,3>;
using Edge = std::array<int32_t,2>;
struct Mesh {
  int level=0;
  std::vector<Vec> xyz;
  std::vector<Face> faces;
  std::vector<Edge> edges;
  std::vector<double> area;
  // Inverse rows of [a b c], ordered four children per parent at each level.
  std::vector<std::vector<std::array<Vec,3>>> hierarchy;
  Atlas atlas;
};
struct Result {
  QString error;
  QString forward_method;
  Mesh mesh;
  PlaneSnapshot source;
  GeoGrid::Grid grid;
  std::vector<double> node_values,edge_values,face_values,restored,residual;
  std::vector<size_t> forward_offset;
  std::vector<int32_t> forward_source; // Original row-major source indices.
  std::vector<double> forward_weight;
  size_t compared=0;
  double bias=0,mae=0,rmse=0;
  std::shared_ptr<void> storage_lease;
};
using Cancel = std::function<bool()>;
QString build_mesh(int level,Mesh &mesh,const Cancel &cancel={});
QString locate(const Mesh &mesh,const Vec &point,size_t &face,Vec &weights);
std::shared_ptr<Result> analyze(const PlaneSnapshot &source,const GeoGrid::Grid &grid,int level,
                              const Cancel &cancel={});
PlaneSnapshot demo(GeoGrid::Grid &grid);
quint64 estimate_bytes(size_t pixels,int level);
// Writes a new file only. Call on GUI/CLI thread, never in a numerical worker.
QString export_netcdf(const QString &path,const Result &result);
int command(const QStringList &arguments);
}

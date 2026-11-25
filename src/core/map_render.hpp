// Pixel-centre inverse sampling, original plane indices and bounded overlay geometry.
#pragma once
#include "map_projection.hpp"
#include "geo_grid.hpp"
#include "coastline.hpp"
#include <QImage>
#include <QLineF>
#include <functional>

namespace MapRender {
enum class State:quint8 { Valid, Missing, Masked, ProjectionOutside, ClipOutside, SourceOutside };
struct Geometry {std::vector<QLineF> graticule,coast;QString diagnostic;std::shared_ptr<void> storage_lease;};
struct Input {
  std::shared_ptr<const GeoGrid::Grid> grid;
  std::shared_ptr<const std::vector<double>> values,original;
  std::shared_ptr<void> source_lease,output_lease;
  std::shared_ptr<const Coastline::Data> coastline;
  std::shared_ptr<const Geometry> geometry;
  MapProjection::Spec spec;
  MapProjection::Viewport viewport;
  QImage source;
};
struct Output {
  QImage image;
  std::vector<qint64> indices;
  std::vector<quint8> states;
  std::shared_ptr<const Geometry> geometry;
  std::shared_ptr<void> lease;
  MapProjection::Viewport viewport;
  QString error;
  quint64 covered=0;
};
std::shared_ptr<Output> render(const Input &input,const std::function<bool()> &cancelled={});
QString state_name(State state);
}

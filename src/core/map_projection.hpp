// ADR-0004: pure point transforms; external coordinates are lon/lat degrees.
#pragma once
#include <QString>
#include <QPointF>
#include <memory>

namespace MapProjection {
enum class Mode { Source, LonLat, NorthPolar, SouthPolar, Lambert, Mercator };
struct EarthModel {
  double a=0,b=0;
  QString evidence;
  bool valid() const;
};
struct Spec {
  Mode mode=Mode::Source;
  double lon0=0,lat0=0,lat1=30,lat2=60,lat_ts=0;
  double west=-180,span=360,south=-90,north=90;
  EarthModel earth;
};
QString name(Mode mode);
Spec preset(Mode mode,const EarthModel &earth={});
QString validate(const Spec &spec);
double unwrap(double longitude,double west);
bool contains(const Spec &spec,double longitude,double latitude);
class Transform {
public:
  explicit Transform(const Spec &spec,const QString &resource_path={});
  ~Transform();
  Transform(const Transform&)=delete;
  Transform &operator=(const Transform&)=delete;
  QString error() const;
  QString operation_definition() const;
  bool forward(double longitude,double latitude,QPointF &point) const;
  bool inverse(const QPointF &point,double &longitude,double &latitude) const;
  static QString database_status(const QString &path);
  static EarthModel model_from_wkt(const QString &wkt,QString &error);
private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
struct Viewport {
  double xmin=0,xmax=0,ymin=0,ymax=0,dpr=1;
  int width=0,height=0;
  QPointF at(double x,double y) const;
  QPointF pixel(const QPointF &projected) const;
};
Viewport fit(const Spec &spec,int width,int height,double dpr,QString &error);
}

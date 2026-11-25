// Verified separable [latitude,longitude] source axes, never inferred from shape alone.
#pragma once
#include "map_projection.hpp"
#include "data_types.hpp"
#include <QJsonObject>
#include <vector>

namespace GeoGrid {
struct Axis {std::vector<double> centers;std::vector<std::pair<double,double>> bounds;bool periodic=false;};
struct Grid {
  Axis latitude,longitude;
  MapProjection::EarthModel earth;
  QString identity,evidence,error;
  QJsonObject source_definition; // Original CF/WKT or edition-aware GRIB evidence.
  bool duplicate_endpoint=false;
};
QString validate(Grid &grid);
QString validate_frame(const Grid &grid,const std::vector<double>&values,const std::vector<double>&original);
qint64 nearest(const Axis &axis,double coordinate,bool longitude=false);
Grid read_netcdf(const QString &file,const QString &variable,size_t rows,size_t cols);
Grid from_grib(const DataSource::PlaneData &plane,const QMap<QString,QString>&keys,const QString &kind);
QJsonObject json(const Grid &grid);
Grid from_json(const QJsonObject &object);
QString stamp(const QString &file);
int worker(const QString &request_file);
}

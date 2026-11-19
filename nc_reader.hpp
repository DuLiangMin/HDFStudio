// Native netCDF read-only helpers. CF diagnostics are advisory.
#pragma once
#include <QString>
#include <QStringList>
#include <netcdf.h>
#include <vector>
#include <string>

struct NcPlane {
  size_t rows=0, cols=0;
  std::vector<double> values;
};
struct FormatProbe {
  QString kind;
  QString detail;
  bool netcdf=false;
};
FormatProbe probe_data_format(const QString &file);
struct CfDiagnostic {
  QString rule;
  QString severity;
  QString path;
  QString message;
};
struct NcVectorPlane { NcPlane u; NcPlane v; };
struct NcGeographicGrid {
  std::string latitudePath;
  std::string longitudePath;
  std::vector<double> latitude;
  std::vector<double> longitude;
  bool valid = false;
  QString error;
};
struct GeoCoordinate { double latitude; double longitude; bool valid; };
GeoCoordinate geographic_coordinate(const NcGeographicGrid &grid,size_t row,size_t column);
struct NcMesh {
  std::string topologyPath,faceConnectivityPath,latitudePath,longitudePath;
  std::vector<double> latitude,longitude,faceValues;
  std::vector<size_t> faceNodes;
  size_t faceCount=0,nodeCount=0,nodesPerFace=0;
  bool valid=false;
  QString error;
};
bool valid_vector_pair(const NcVectorPlane &planes);

// netCDF-C on Windows opens files through the ANSI code page (936 on a Chinese system), so a path
// with Chinese characters must be handed over in the local 8-bit encoding. UTF-8 is used only when
// the local code page cannot represent the path (then no ANSI path exists anyway).
inline QByteArray nc_path_bytes(const QString &file) {
  const QByteArray local=file.toLocal8Bit();
  return QString::fromLocal8Bit(local)==file ? local : file.toUtf8();
}

struct NcReader {
  int id = -1;
  explicit NcReader(const QString &file) { if(nc_open(nc_path_bytes(file).constData(), NC_NOWRITE, &id)!=NC_NOERR) id=-1; }
  ~NcReader() { if(id >= 0) nc_close(id); }
  NcReader(const NcReader&) = delete;
  bool locate(const std::string &path, int &group, int &variable) const;
  QString cf_summary() const;
  QString read_plane(const std::string &path, const std::vector<size_t> &indices, NcPlane &plane) const;
  QString read_vector_planes(const std::string &uPath,const std::string &vPath,
      const std::vector<size_t> &indices,NcVectorPlane &planes) const;
  NcGeographicGrid geographic_grid(const std::string &dataPath,
                                   const std::vector<size_t> &dataShape) const;
  NcMesh read_ugrid_faces(const std::string &dataPath) const;
  std::vector<CfDiagnostic> diagnose_cf18() const;
};

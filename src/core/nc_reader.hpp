// Native netCDF read-only helpers. CF diagnostics are advisory.
#pragma once
#include <QString>
#include <QStringList>
#include <netcdf.h>
#include <vector>
#include <string>
#include "slice_types.hpp"

struct NcPlane {
  size_t rows=0, cols=0;
  std::vector<double> values;
};
struct NcSliceResult {
  NcPlane plane;
  QString reader, error_code, error;
  bool precision_warning = false;
  QStringList diagnostics;
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

// Dimension names of a variable ("/ttr" -> time, lev, lat, lon) through netCDF-C; empty when the
// file is not readable as NetCDF (plain HDF5) or the variable is not found.
std::vector<std::string> nc_dimension_names(const QString &file, const std::string &path);

// CF packing attributes of one variable. Attribute values are as written in the file (stored
// units, CF 8.1); decoded = stored * scale_factor + add_offset is the value shown in the image,
// legend and Mask Range. found=false for plain HDF5 or an unknown variable.
struct NcPacking {
  bool found = false;
  bool has_scale = false, has_offset = false;
  double scale = 1, offset = 0;
  bool has_valid_min = false, has_valid_max = false;
  double valid_min = 0, valid_max = 0;   // stored units, from valid_range or valid_min/valid_max
  QString valid_source;                  // "valid_range", "valid_min/valid_max", "valid_min", "valid_max"
  bool has_fill = false, has_missing = false;
  double fill = 0, missing = 0;          // stored units
  double decode(double stored) const { return stored * scale + offset; }
  bool packed() const { return has_scale || has_offset; }
};
NcPacking nc_packing(const QString &file, const std::string &path);

// Mask Range (Image View and `hdf_cli mask`): finite decoded values inside [start, end], inclusive,
// become NaN; returns how many were masked. start may be -inf, end +inf. Decoded values only.
size_t nc_mask_range(std::vector<double> &decoded_values, double start, double end);

// One Mask Range field: empty text (or "-") = the open end (-inf for start, +inf for end);
// false when the text is not a finite number.
bool nc_mask_bound(const QString &text, double open_end, double &value);
// Properties lines that state the decoding and give valid range / fill values in decoded units
// next to the stored ones, e.g. "valid_range decoded: [0, 9999] (stored [0, 99990000])".
QStringList nc_packing_lines(const NcPacking &packing);

struct NcReader {
  int id = -1;
  explicit NcReader(const QString &file) { if(nc_open(nc_path_bytes(file).constData(), NC_NOWRITE, &id)!=NC_NOERR) id=-1; }
  ~NcReader() { if(id >= 0) nc_close(id); }
  NcReader(const NcReader&) = delete;
  bool locate(const std::string &path, int &group, int &variable) const;
  QString cf_summary() const;
  QString read_plane(const std::string &path, const std::vector<size_t> &indices, NcPlane &plane) const;
  NcSliceResult read_slice(const std::string &path, const NcSliceSpec &spec) const;
  NcSliceResult read_coordinate(const std::string &path,size_t index=size_t(-1)) const;
  QString read_vector_planes(const std::string &uPath,const std::string &vPath,
      const std::vector<size_t> &indices,NcVectorPlane &planes) const;
  NcGeographicGrid geographic_grid(const std::string &dataPath,
                                   const std::vector<size_t> &dataShape) const;
  NcMesh read_ugrid_faces(const std::string &dataPath) const;
  std::vector<CfDiagnostic> diagnose_cf18() const;
};

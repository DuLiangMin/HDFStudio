// Read-only object and attribute summaries for the Properties dialog, `hdf_cli props` and tests.
// HDF5 / NetCDF-4 files are read with HDF5, NetCDF-3 (CDF-1/2/5) files with netCDF-C.
#pragma once
#include <QString>
#include <QStringList>
#include <vector>

namespace HdfProperties {
struct Summary {
  QString reader = "HDF5";   // library that produced the summary: "HDF5" or "netCDF-C"
  QString objectType;
  QString dataType;
  QString shape;
  QString dimensions;        // NetCDF only, as Python netCDF4: "ttr(time, lev, lat, lon)" or
                             // for the root group "time(60), lev(100), lat(121), lon(240)"
  QString unlimitedDimensions;
  QString storage;
  QString compression;
  QStringList attributes;
  QStringList decoding;      // datasets: scale_factor/add_offset rule and valid range / fill in decoded units
  quint64 elements = 0;
  quint64 bytes = 0;
  bool byteCountOverflow = false;
  bool attributeListTruncated = false;
  bool valid = false;
};

Summary inspect(const QString &filePath, const QString &objectPath);
// The detail lines shown under the tree-based fields of the Properties dialog; `hdf_cli props`
// prints the same lines so a GUI report can be reproduced on the command line.
QStringList describe(const Summary &summary);
QString formatAttribute(const QString &name, const QString &type,
                        const QString &shape, const QString &value,
                        int maximumCharacters = 256);
}

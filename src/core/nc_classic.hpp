// Read-only access to NetCDF-3 family files (CDF-1 classic, CDF-2 64-bit offset, CDF-5 64-bit data)
// through netCDF-C. These files have no HDF5 container, so H5Fopen cannot read them; the GUI tree,
// grid, image, properties and text export switch to this module when is_classic_netcdf_file() is true.
// Classic files have one root group only: variables are addressed as "/name".
#pragma once
#include <QString>
#include <netcdf.h>
#include <string>
#include <vector>

// True when the first four bytes are "CDF" followed by version 1, 2 or 5. No library call, so it
// is cheap enough to ask before every read.
bool is_classic_netcdf_file(const QString &file);

struct NcClassicAttribute {
  std::string name;
  nc_type type = NC_NAT;
  size_t length = 0;
};

struct NcClassicDimension {
  std::string name;
  size_t length = 0;                   // current length; for the unlimited dimension the record count
  bool unlimited = false;
};

struct NcClassicVariable {
  std::string name;
  nc_type type = NC_NAT;
  std::vector<std::string> dimension_names;  // same order as shape, e.g. time, lev, lat, lon
  std::vector<size_t> shape;           // current length of every dimension, unlimited one included
  bool record = false;                 // first dimension is the unlimited (record) dimension
  std::vector<NcClassicAttribute> attributes;
};

// Root group of the file as netCDF-C sees it. For a NetCDF-4 file only the root group is listed.
struct NcClassicFile {
  QString format;                             // "NETCDF3_CLASSIC", "NETCDF4", ... as in Python netCDF4
  std::vector<NcClassicDimension> dimensions; // in definition order
  std::vector<NcClassicAttribute> global_attributes;
  std::vector<NcClassicVariable> variables;   // in definition order
  QString error;                              // empty on success
};

NcClassicFile nc_classic_inspect(const QString &file);

// Storage description of an external netCDF type, independent of HDF5.
struct NcTypeInfo {
  enum Kind { Integer, Float, Text, Other } kind = Other;
  size_t size = 0;
  bool is_signed = true;
};
NcTypeInfo nc_type_info(nc_type type);

// Reads start/count of a variable into `buffer` in the variable's own type (no conversion).
// Empty start and count read a scalar. Returns an empty string on success, else the reason.
QString nc_classic_read_variable(const QString &file, const std::string &variable,
                                 const std::vector<size_t> &start,
                                 const std::vector<size_t> &count, void *buffer);

// Reads a whole attribute in its own type; `variable` empty selects a global attribute.
QString nc_classic_read_attribute(const QString &file, const std::string &variable,
                                  const std::string &name, void *buffer);

// Small helpers used by the image view for flag_values / flag_meanings.
std::vector<double> nc_classic_numeric_attribute(const QString &file, const std::string &variable,
                                                 const std::string &name);
QString nc_classic_text_attribute(const QString &file, const std::string &variable,
                                  const std::string &name);

// Attribute value as Python netCDF4 shows it: text as is, one number plain, several numbers as
// "[a, b, ...]"; float32 values in their shortest round-trip form (0.1, not 0.100000001).
// More than `maximum_values` numbers end with ", ... (N values)".
QString nc_classic_attribute_value(const QString &file, const std::string &variable,
                                   const NcClassicAttribute &attribute, size_t maximum_values = 64);

// Type name as numpy / netCDF4 prints it: float32, int16; text is "char" as in ncdump.
QString nc_classic_type_name(nc_type type);

// Plain-text listing in the layout of `print(netCDF4.Dataset(file))` followed by every variable,
// i.e. what the Datasets tree shows. Used by `hdf_cli info` to compare with Python line by line.
QString nc_classic_listing(const QString &file);

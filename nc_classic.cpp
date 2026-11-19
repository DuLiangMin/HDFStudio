#include "nc_classic.hpp"
#include "nc_reader.hpp"
#include <QFile>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace {
// Closes the netCDF id on every exit path.
struct OpenFile {
  int id = -1;
  explicit OpenFile(const QString &file) {
    if(nc_open(nc_path_bytes(file).constData(), NC_NOWRITE, &id) != NC_NOERR) id = -1;
  }
  ~OpenFile() { if(id >= 0) nc_close(id); }
};

// "/name" or "name" -> variable id, NC_GLOBAL for an empty name.
bool find_variable(int ncid, const std::string &variable, int &varid) {
  if(variable.empty() || variable == "/") { varid = NC_GLOBAL; return true; }
  const std::string name = variable[0] == '/' ? variable.substr(1) : variable;
  return nc_inq_varid(ncid, name.c_str(), &varid) == NC_NOERR;
}

std::vector<NcClassicAttribute> read_attribute_list(int ncid, int varid) {
  std::vector<NcClassicAttribute> list;
  int count = 0;
  const int status = varid == NC_GLOBAL ? nc_inq_natts(ncid, &count) : nc_inq_varnatts(ncid, varid, &count);
  if(status != NC_NOERR) return list;
  for(int index = 0; index < count; ++index) {
    char name[NC_MAX_NAME + 1] = {};
    if(nc_inq_attname(ncid, varid, index, name) != NC_NOERR) continue;
    NcClassicAttribute attribute;
    attribute.name = name;
    if(nc_inq_att(ncid, varid, name, &attribute.type, &attribute.length) != NC_NOERR) continue;
    list.push_back(attribute);
  }
  return list;
}
}

bool is_classic_netcdf_file(const QString &file) {
  QFile input(file);
  if(!input.open(QIODevice::ReadOnly)) return false;
  const QByteArray head = input.read(4);
  return head.size() == 4 && head.startsWith("CDF") && (head[3] == 1 || head[3] == 2 || head[3] == 5);
}

NcTypeInfo nc_type_info(nc_type type) {
  switch(type) {
    case NC_BYTE:   return {NcTypeInfo::Integer, 1, true};
    case NC_UBYTE:  return {NcTypeInfo::Integer, 1, false};
    case NC_SHORT:  return {NcTypeInfo::Integer, 2, true};
    case NC_USHORT: return {NcTypeInfo::Integer, 2, false};
    case NC_INT:    return {NcTypeInfo::Integer, 4, true};
    case NC_UINT:   return {NcTypeInfo::Integer, 4, false};
    case NC_INT64:  return {NcTypeInfo::Integer, 8, true};
    case NC_UINT64: return {NcTypeInfo::Integer, 8, false};
    case NC_FLOAT:  return {NcTypeInfo::Float, 4, true};
    case NC_DOUBLE: return {NcTypeInfo::Float, 8, true};
    case NC_CHAR:   return {NcTypeInfo::Text, 1, true};
    default:        return {NcTypeInfo::Other, 0, true};
  }
}

NcClassicFile nc_classic_inspect(const QString &file) {
  NcClassicFile result;
  OpenFile opened(file);
  if(opened.id < 0) { result.error = QString("netCDF-C cannot open the file"); return result; }
  int dimensions = 0, variables = 0, unlimited = -1;
  if(nc_inq(opened.id, &dimensions, &variables, nullptr, &unlimited) != NC_NOERR) {
    result.error = QString("Cannot read the NetCDF header"); return result;
  }
  int format = 0;
  if(nc_inq_format(opened.id, &format) == NC_NOERR) {
    switch(format) {
      case NC_FORMAT_CLASSIC:         result.format = "NETCDF3_CLASSIC"; break;
      case NC_FORMAT_64BIT_OFFSET:    result.format = "NETCDF3_64BIT_OFFSET"; break;
      case NC_FORMAT_CDF5:            result.format = "NETCDF3_64BIT_DATA"; break;
      case NC_FORMAT_NETCDF4:         result.format = "NETCDF4"; break;
      case NC_FORMAT_NETCDF4_CLASSIC: result.format = "NETCDF4_CLASSIC"; break;
      default:                        result.format = QString("format %1").arg(format); break;
    }
  }
  // nc_inq_dimids also works for classic files, where the ids are simply 0..n-1
  std::vector<int> dimension_ids(size_t(std::max(0, dimensions)));
  int dimension_count = 0;
  if(dimensions > 0 && nc_inq_dimids(opened.id, &dimension_count, dimension_ids.data(), 0) == NC_NOERR) {
    for(int index = 0; index < dimension_count; ++index) {
      char name[NC_MAX_NAME + 1] = {};
      NcClassicDimension dimension;
      if(nc_inq_dim(opened.id, dimension_ids[size_t(index)], name, &dimension.length) != NC_NOERR) continue;
      dimension.name = name;
      dimension.unlimited = dimension_ids[size_t(index)] == unlimited;
      result.dimensions.push_back(dimension);
    }
  }
  result.global_attributes = read_attribute_list(opened.id, NC_GLOBAL);
  for(int varid = 0; varid < variables; ++varid) {
    char name[NC_MAX_NAME + 1] = {};
    nc_type type = NC_NAT;
    int rank = 0;
    int dimids[NC_MAX_VAR_DIMS] = {};
    if(nc_inq_var(opened.id, varid, name, &type, &rank, dimids, nullptr) != NC_NOERR) continue;
    NcClassicVariable variable;
    variable.name = name;
    variable.type = type;
    for(int axis = 0; axis < rank; ++axis) {
      size_t length = 0;
      char dimension_name[NC_MAX_NAME + 1] = {};
      nc_inq_dim(opened.id, dimids[axis], dimension_name, &length);
      variable.dimension_names.push_back(dimension_name);
      variable.shape.push_back(length);
      if(axis == 0 && dimids[axis] == unlimited) variable.record = true;
    }
    variable.attributes = read_attribute_list(opened.id, varid);
    result.variables.push_back(std::move(variable));
  }
  return result;
}

QString nc_classic_read_variable(const QString &file, const std::string &variable,
                                 const std::vector<size_t> &start,
                                 const std::vector<size_t> &count, void *buffer) {
  OpenFile opened(file);
  if(opened.id < 0) return "netCDF-C cannot open the file";
  int varid = -1;
  if(!find_variable(opened.id, variable, varid) || varid == NC_GLOBAL) return "Variable not found";
  const int status = start.empty()
      ? nc_get_var(opened.id, varid, buffer)
      : nc_get_vara(opened.id, varid, start.data(), count.data(), buffer);
  return status == NC_NOERR ? QString() : QString::fromUtf8(nc_strerror(status));
}

QString nc_classic_read_attribute(const QString &file, const std::string &variable,
                                  const std::string &name, void *buffer) {
  OpenFile opened(file);
  if(opened.id < 0) return "netCDF-C cannot open the file";
  int varid = -1;
  if(!find_variable(opened.id, variable, varid)) return "Variable not found";
  const int status = nc_get_att(opened.id, varid, name.c_str(), buffer);
  return status == NC_NOERR ? QString() : QString::fromUtf8(nc_strerror(status));
}

std::vector<double> nc_classic_numeric_attribute(const QString &file, const std::string &variable,
                                                 const std::string &name) {
  std::vector<double> values;
  OpenFile opened(file);
  int varid = -1;
  size_t length = 0;
  if(opened.id < 0 || !find_variable(opened.id, variable, varid) ||
     nc_inq_attlen(opened.id, varid, name.c_str(), &length) != NC_NOERR || length == 0 || length > 65536) return values;
  nc_type type = NC_NAT;
  if(nc_inq_atttype(opened.id, varid, name.c_str(), &type) != NC_NOERR ||
     nc_type_info(type).kind == NcTypeInfo::Text || nc_type_info(type).kind == NcTypeInfo::Other) return values;
  values.resize(length);
  if(nc_get_att_double(opened.id, varid, name.c_str(), values.data()) != NC_NOERR) values.clear();
  return values;
}

QString nc_classic_text_attribute(const QString &file, const std::string &variable,
                                  const std::string &name) {
  OpenFile opened(file);
  int varid = -1;
  nc_type type = NC_NAT;
  size_t length = 0;
  if(opened.id < 0 || !find_variable(opened.id, variable, varid) ||
     nc_inq_att(opened.id, varid, name.c_str(), &type, &length) != NC_NOERR ||
     type != NC_CHAR || length > 1024 * 1024) return {};
  QByteArray text(int(length), '\0');
  if(nc_get_att_text(opened.id, varid, name.c_str(), text.data()) != NC_NOERR) return {};
  return QString::fromUtf8(text);
}

QString nc_classic_type_name(nc_type type) {
  switch(type) {
    case NC_BYTE:   return "int8";
    case NC_UBYTE:  return "uint8";
    case NC_SHORT:  return "int16";
    case NC_USHORT: return "uint16";
    case NC_INT:    return "int32";
    case NC_UINT:   return "uint32";
    case NC_INT64:  return "int64";
    case NC_UINT64: return "uint64";
    case NC_FLOAT:  return "float32";
    case NC_DOUBLE: return "float64";
    case NC_CHAR:   return "char";
    case NC_STRING: return "str";
    default:        return QString("nc_type %1").arg(int(type));
  }
}

namespace {
// Python/numpy style: the fewest significant digits that read back to the same value (float32
// compared as float32), positional for 1e-4 <= |x| < 1e16 with ".0" on whole numbers,
// scientific otherwise (1e-05, 9.96921e+36).
QString python_float_text(double value, bool single_precision) {
  if(!std::isfinite(value)) return std::isnan(value) ? QString("nan") : (value > 0 ? QString("inf") : QString("-inf"));
  if(value == 0) return std::signbit(value) ? QString("-0.0") : QString("0.0");
  const int maximum_digits = single_precision ? 9 : 17;
  int digits = 1;
  for(; digits < maximum_digits; ++digits) {
    const double back = QString::number(value, 'e', digits - 1).toDouble();
    const bool same = single_precision ? float(back) == float(value) : back == value;
    if(same) break;
  }
  const int exponent = int(std::floor(std::log10(std::fabs(value))));
  if(exponent < -4 || exponent >= 16) {
    QString text = QString::number(value, 'e', digits - 1);   // e.g. 9.96921e+36
    const int mark = text.indexOf('e');
    QString mantissa = text.left(mark);
    if(mantissa.contains('.')) {
      while(mantissa.endsWith('0')) mantissa.chop(1);
      if(mantissa.endsWith('.')) mantissa.chop(1);
    }
    return mantissa + text.mid(mark);
  }
  QString text = QString::number(value, 'f', std::max(0, digits - 1 - exponent));
  if(!text.contains('.')) text += ".0";
  return text;
}
}

QString nc_classic_attribute_value(const QString &file, const std::string &variable,
                                   const NcClassicAttribute &attribute, size_t maximum_values) {
  const NcTypeInfo info = nc_type_info(attribute.type);
  if(info.kind == NcTypeInfo::Text) return nc_classic_text_attribute(file, variable, attribute.name);
  if(info.kind == NcTypeInfo::Other) return QString("(%1 not shown)").arg(nc_classic_type_name(attribute.type));
  OpenFile opened(file);
  int varid = -1;
  if(opened.id < 0 || !find_variable(opened.id, variable, varid)) return QString("(unreadable)");
  const size_t shown = std::min(attribute.length, maximum_values);
  QStringList parts;
  if(info.kind == NcTypeInfo::Float) {
    std::vector<double> values(attribute.length);
    if(attribute.length && nc_get_att_double(opened.id, varid, attribute.name.c_str(), values.data()) != NC_NOERR)
      return QString("(unreadable)");
    for(size_t i = 0; i < shown; ++i) parts << python_float_text(values[i], info.size == 4);
  } else if(info.is_signed) {
    std::vector<long long> values(attribute.length);
    if(attribute.length && nc_get_att_longlong(opened.id, varid, attribute.name.c_str(), values.data()) != NC_NOERR)
      return QString("(unreadable)");
    for(size_t i = 0; i < shown; ++i) parts << QString::number(values[i]);
  } else {
    std::vector<unsigned long long> values(attribute.length);
    if(attribute.length && nc_get_att_ulonglong(opened.id, varid, attribute.name.c_str(), values.data()) != NC_NOERR)
      return QString("(unreadable)");
    for(size_t i = 0; i < shown; ++i) parts << QString::number(values[i]);
  }
  if(attribute.length == 1) return parts.value(0);
  QString text = "[" + parts.join(", ");
  if(shown < attribute.length) text += QString(", ... (%1 values)").arg(attribute.length);
  return text + "]";
}

QString nc_classic_listing(const QString &file) {
  const NcClassicFile info = nc_classic_inspect(file);
  if(!info.error.isEmpty()) return info.error + "\n";
  QStringList lines;
  lines << QString("root group (file format %1):").arg(info.format);
  for(const NcClassicAttribute &attribute : info.global_attributes)
    lines << QString("    %1: %2").arg(QString::fromStdString(attribute.name),
                                       nc_classic_attribute_value(file, std::string(), attribute));
  QStringList sizes, unlimited;
  for(const NcClassicDimension &dimension : info.dimensions) {
    sizes << QString("%1(%2)").arg(QString::fromStdString(dimension.name)).arg(qulonglong(dimension.length));
    if(dimension.unlimited) unlimited << QString::fromStdString(dimension.name);
  }
  lines << "    dimensions(sizes): " + sizes.join(", ");
  QStringList declarations;
  for(const NcClassicVariable &variable : info.variables) {
    QStringList names;
    for(const std::string &name : variable.dimension_names) names << QString::fromStdString(name);
    declarations << QString("%1 %2(%3)").arg(nc_classic_type_name(variable.type),
                                             QString::fromStdString(variable.name), names.join(", "));
  }
  lines << "    variables(dimensions): " + declarations.join(", ");
  lines << "unlimited dimensions: " + unlimited.join(", ");
  for(size_t index = 0; index < info.variables.size(); ++index) {
    const NcClassicVariable &variable = info.variables[index];
    QStringList shape;
    for(size_t length : variable.shape) shape << QString::number(qulonglong(length));
    lines << QString();
    lines << "variable " + declarations[int(index)];
    for(const NcClassicAttribute &attribute : variable.attributes)
      lines << QString("    %1: %2").arg(QString::fromStdString(attribute.name),
                                         nc_classic_attribute_value(file, variable.name, attribute));
    lines << QString("current shape = (%1%2)").arg(shape.join(", "), shape.size() == 1 ? "," : "");
  }
  return lines.join('\n') + "\n";
}

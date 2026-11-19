// hdf_cli: console front end for debugging file reading without the GUI.
// Every command calls the same functions as HDFStudio, so a problem seen in the GUI can be
// reproduced, and compared with Python netCDF4, from the command line:
//   probe   format detection used for File Info          (probe_data_format)
//   info    netCDF4-style listing of the Datasets content (nc_classic_listing, netCDF-C)
//   props   lines of the Properties dialog               (HdfProperties::inspect/describe)
//   export  rows of "Export as text..."                  (HdfTextExport::write)
//   csv     one 2-D plane as the Image View reads it      (NcReader::read_plane, applies
//           _FillValue / missing_value / scale_factor / add_offset)
// Examples:
//   hdf_cli probe  data.nc
//   hdf_cli info   data.nc
//   hdf_cli props  data.nc /ttr
//   hdf_cli export data.nc /lat            (to the console)
//   hdf_cli export data.nc /ttr ttr.txt
//   hdf_cli csv    data.nc /ttr plane.csv 7 42
// Exit codes: 0 ok, 2 usage, 3 cannot read, 4 cannot write the output.
#include "nc_reader.hpp"
#include "nc_classic.hpp"
#include "properties_inspector.hpp"
#include "text_export.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <cmath>
#include <cstdio>

namespace {
void print_usage() {
  QTextStream err(stderr);
  err << "Usage:\n"
         "  hdf_cli probe  <file>\n"
         "  hdf_cli info   <file>\n"
         "  hdf_cli props  <file> <path>\n"
         "  hdf_cli export <file> <path> [out.txt]\n"
         "  hdf_cli csv    <file> <path> <new.csv> [index ...]\n";
}

// UTF-8 bytes to stdout, so attribute text survives redirection to a file.
void print_text(const QString &text) {
  const QByteArray bytes = text.toUtf8();
  fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
  fflush(stdout);
}

int command_probe(const QString &file) {
  const FormatProbe probe = probe_data_format(file);
  print_text(QString("kind: %1\nnetcdf: %2\nclassic (read with netCDF-C): %3\ndetail: %4\n")
                 .arg(probe.kind, probe.netcdf ? "yes" : "no",
                      is_classic_netcdf_file(file) ? "yes" : "no", probe.detail));
  return probe.kind.isEmpty() ? 3 : 0;
}

int command_info(const QString &file) {
  const NcClassicFile info = nc_classic_inspect(file);
  if(!info.error.isEmpty()) { QTextStream(stderr) << info.error << '\n'; return 3; }
  print_text(nc_classic_listing(file));
  return 0;
}

int command_props(const QString &file, const QString &path) {
  const HdfProperties::Summary summary = HdfProperties::inspect(file, path);
  QStringList lines;
  lines << QString("Path: %1").arg(path);
  if(summary.valid) {
    lines << QString("Shape: %1").arg(summary.shape);
    lines << QString("Elements: %1").arg(summary.elements);
    lines << QString("Uncompressed size: %1 bytes").arg(summary.bytes);
  }
  lines.append(HdfProperties::describe(summary));
  print_text(lines.join('\n') + "\n");
  return summary.valid ? 0 : 3;
}

int command_export(const QString &file, const QString &path, const QString &target) {
  QFile output;
  if(target.isEmpty()) {
    if(!output.open(stdout, QIODevice::WriteOnly)) return 4;
  } else {
    output.setFileName(target);
    if(!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
      QTextStream(stderr) << target << ": " << output.errorString() << '\n';
      return 4;
    }
  }
  const HdfTextExport::Result result = HdfTextExport::write(file, path, output);
  output.close();
  if(!result.ok) {
    QTextStream(stderr) << result.error << '\n';
    if(!target.isEmpty()) QFile::remove(target);
    return 3;
  }
  QTextStream(stderr) << "values written: " << result.valuesWritten << '\n';
  return 0;
}

int command_csv(const QStringList &args) {
  QTextStream err(stderr);
  std::vector<size_t> indices;
  for(int i = 5; i < args.size(); ++i) {
    bool ok = false;
    const qulonglong index = args[i].toULongLong(&ok);
    if(!ok) { print_usage(); return 2; }
    indices.push_back(size_t(index));
  }
  NcReader reader(args[2]);
  NcPlane plane;
  const QString error = reader.read_plane(args[3].toStdString(), indices, plane);
  if(!error.isEmpty()) { err << error << '\n'; return 3; }
  QFile file(args[4]);
  if(!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { err << file.errorString() << '\n'; return 4; }
  QTextStream out(&file);
  out.setRealNumberPrecision(17);
  for(size_t r = 0; r < plane.rows; ++r) {
    for(size_t c = 0; c < plane.cols; ++c) {
      if(c) out << ',';
      const double value = plane.values[r * plane.cols + c];
      if(std::isfinite(value)) out << value;
    }
    out << '\n';
  }
  out.flush();
  if(out.status() != QTextStream::Ok || !file.flush()) { file.remove(); return 4; }
  return 0;
}
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  const QString command = args.value(1);
  if(command == "probe" && args.size() == 3) return command_probe(args[2]);
  if(command == "info" && args.size() == 3) return command_info(args[2]);
  if(command == "props" && args.size() == 4) return command_props(args[2], args[3]);
  if(command == "export" && (args.size() == 4 || args.size() == 5))
    return command_export(args[2], args[3], args.value(4));
  if(command == "csv" && args.size() >= 5) return command_csv(args);
  print_usage();
  return 2;
}

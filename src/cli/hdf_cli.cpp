// hdf_cli: console front end for debugging file reading without the GUI.
// Every command calls the same functions as HDFStudio, so a problem seen in the GUI can be
// reproduced, and compared with Python netCDF4, from the command line:
//   probe   format detection used for File Info          (probe_data_format)
//   info    netCDF4-style listing of the Datasets content (nc_classic_listing, netCDF-C)
//   props   lines of the Properties dialog               (HdfProperties::inspect/describe)
//   export  rows of "Export as text..."                  (HdfTextExport::write)
//   csv     one 2-D plane as the Image View reads it      (NcReader::read_plane, applies
//           _FillValue / missing_value / scale_factor / add_offset)
//   mask    Image View Mask Range on one decoded plane     (nc_mask_range / nc_mask_bound):
//           start and end are decoded values, "-" = open end (-inf / +inf); prints the
//           decoding rule, counts and min/max of the values that remain
//   hist    histogram of one decoded plane                (nc_read_plane_decoded / nc_histogram):
//           the plane is sampled at random, default one value in twenty (0.05); prints the
//           sampled count, range, mean, stddev and one bar per bin
// Examples:
//   hdf_cli probe  data.nc
//   hdf_cli info   data.nc
//   hdf_cli props  data.nc /ttr
//   hdf_cli export data.nc /lat            (to the console)
//   hdf_cli export data.nc /ttr ttr.txt
//   hdf_cli csv    data.nc /ttr plane.csv 7 42
//   hdf_cli mask   cmpa.nc /precipitation 9999 - 0
//   hdf_cli hist   cmpa.nc /precipitation 0.05 32 0
// Exit codes: 0 ok, 2 usage, 3 cannot read, 4 cannot write the output.
#include "nc_reader.hpp"
#include "nc_classic.hpp"
#include "plane_reader.hpp"
#include "properties_inspector.hpp"
#include "text_export.hpp"
#include "color_map.hpp"
#include "image_metrics.hpp"
#include "texture_entropy.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
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
         "  hdf_cli csv    <file> <path> <new.csv> [index ...]\n"
         "  hdf_cli mask   <file> <path> <start|-> <end|-> [index ...]   (decoded values)\n"
         "  hdf_cli hist   <file> <path> [keep_fraction] [bins] [index ...]   (default 0.05 = 1 in 20)\n";
   err << "  hdf_cli entropy <file> <path> [bins=256] [index ...]\n";
   err << "  hdf_cli texture-entropy <file> <path> <output.csv> [index ...]\n";
   err << "  hdf_cli corr <anchor_file> <anchor_var> <target_file> <target_var> [--anchor-idx i,j] [--target-idx i,j]\n";
   err << "  hdf_cli cmap [palette-id [fraction]]   (list or sample bundled color maps)\n"
         "  hdf_cli rgb <file.rgb> <unit|byte>     (validate local RGB without installing it)\n";
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
   return probe.kind.isEmpty() || probe.kind == "Unreadable" || probe.kind == "Unknown" ||
          probe.kind.contains("open failed") ? 3 : 0;
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

int command_mask(const QStringList &args) {
  QTextStream err(stderr);
  double start = 0, end = 0;
  if(!nc_mask_bound(args[4], -INFINITY, start) || !nc_mask_bound(args[5], INFINITY, end) || start > end) {
    print_usage(); return 2;
  }
  std::vector<size_t> indices;
  for(int i = 6; i < args.size(); ++i) {
    bool ok = false;
    const qulonglong index = args[i].toULongLong(&ok);
    if(!ok) { print_usage(); return 2; }
    indices.push_back(size_t(index));
  }
  NcReader reader(args[2]);
  NcPlane plane;
  const QString error = reader.read_plane(args[3].toStdString(), indices, plane);
  if(!error.isEmpty()) { err << error << '\n'; return 3; }
  auto summary = [](const std::vector<double> &values, size_t &count, double &low, double &high) {
    count = 0; low = INFINITY; high = -INFINITY;
    for(double value : values) {
      if(!std::isfinite(value)) continue;
      ++count; low = std::min(low, value); high = std::max(high, value);
    }
  };
  auto number = [](double value) {
    if(std::isinf(value)) return value < 0 ? QString("-inf") : QString("+inf");
    return QString::number(value, 'g', 15);
  };
  size_t before = 0, after = 0;
  double low = 0, high = 0;
  summary(plane.values, before, low, high);
  QStringList lines = nc_packing_lines(nc_packing(args[2], args[3].toStdString()));
  lines << QString("plane: %1 x %2").arg(plane.rows).arg(plane.cols);
  lines << QString("valid before mask: %1  min %2  max %3").arg(before).arg(number(low), number(high));
  const size_t masked = nc_mask_range(plane.values, start, end);
  summary(plane.values, after, low, high);
  lines << QString("mask [%1, %2] decoded: %3 masked").arg(number(start), number(end)).arg(masked);
  lines << QString("valid after mask: %1  min %2  max %3").arg(after)
               .arg(after ? number(low) : QString("none"), after ? number(high) : QString("none"));
  print_text(lines.join('\n') + "\n");
  return 0;
}

NcPlaneResult read_metrics_plane(const QString &file, const QString &path, const std::vector<size_t> &indices) {
  std::vector<hsize_t> shape;
  if(probe_data_format(file).netcdf) {
    NcReader reader(file);
    int group=-1, variable=-1, rank=0;
    if(!reader.locate(path.toStdString(),group,variable)) return {{},{},"Variable not found"};
    if(nc_inq_varndims(group,variable,&rank)!=NC_NOERR || rank<2) return {{},{},"A metric needs at least two dimensions"};
    std::vector<int> dimensions(size_t(rank),0);
    if(nc_inq_vardimid(group,variable,dimensions.data())!=NC_NOERR) return {{},{},"Cannot read dimensions"};
    for(int dimension:dimensions) {
      size_t length=0;
      if(nc_inq_dimlen(group,dimension,&length)!=NC_NOERR) return {{},{},"Cannot read dimensions"};
      shape.push_back(hsize_t(length));
    }
  } else {
    const hid_t handle=H5Fopen(file.toLocal8Bit().constData(),H5F_ACC_RDONLY,H5P_DEFAULT);
    if(handle<0) return {{},{},"Cannot open file"};
    const hid_t dataset=H5Dopen2(handle,path.toUtf8().constData(),H5P_DEFAULT);
    const hid_t space=dataset<0 ? -1 : H5Dget_space(dataset);
    const int rank=space<0 ? -1 : H5Sget_simple_extent_ndims(space);
    if(rank>=2) { shape.resize(size_t(rank)); H5Sget_simple_extent_dims(space,shape.data(),nullptr); }
    if(space>=0) H5Sclose(space);
    if(dataset>=0) H5Dclose(dataset);
    H5Fclose(handle);
    if(rank<2) return {{},{},"A metric needs a dataset with at least two dimensions"};
  }
  if(indices.size()!=shape.size()-2) return {{},{},"Specify one zero-based index for every leading axis"};
  for(size_t i=0;i<indices.size();++i) if(indices[i]>=shape[i]) return {{},{},"Index outside axis"};
  return nc_read_plane_decoded(file,path.toStdString(),shape,indices);
}

int command_corr(const QStringList &args) {
  std::vector<size_t> anchor_indices,target_indices;
  bool anchor_seen=false,target_seen=false;
  for(int i=6;i<args.size();i+=2) {
    const bool anchor=args[i]=="--anchor-idx", target=args[i]=="--target-idx";
    if((!anchor&&!target) || i+1>=args.size() || (anchor&&anchor_seen) || (target&&target_seen)) return 2;
    if(anchor) anchor_seen=true; else target_seen=true;
    auto &indices=anchor ? anchor_indices : target_indices;
    for(const auto &part:args[i+1].split(',')) {
      bool ok=false;
      const auto index=part.toULongLong(&ok);
      if(!ok || part.startsWith('-')) return 2;
      indices.push_back(size_t(index));
    }
  }
  const auto anchor=read_metrics_plane(args[2],args[3],anchor_indices);
  if(!anchor.error.isEmpty()) { QTextStream(stderr)<<anchor.error<<'\n';return 3; }
  const auto target=read_metrics_plane(args[4],args[5],target_indices);
  if(!target.error.isEmpty()) { QTextStream(stderr)<<target.error<<'\n';return 3; }
  const auto metrics=nc_pair_metrics(anchor.plane.values,anchor.plane.rows,anchor.plane.cols,
                                    target.plane.values,target.plane.rows,target.plane.cols);
  if(!metrics.valid) { QTextStream(stderr)<<metrics.error<<'\n';return 3; }
  print_text(QString("shape: %1x%2\nvalid_pairs: %3\nexcluded_pairs: %4\npearson_r: %5\nrmse: %6\nmae: %7\nbias: %8\n")
    .arg(anchor.plane.rows).arg(anchor.plane.cols).arg(metrics.valid_pairs).arg(metrics.excluded_pairs)
    .arg(metrics.r_valid ? QString::number(metrics.pearson_r,'g',15) : QString("nan"))
    .arg(metrics.rmse,0,'g',15).arg(metrics.mae,0,'g',15).arg(metrics.bias,0,'g',15));
  return 0;
}

int command_texture_entropy(const QStringList &args) {
  std::vector<size_t> indices;
  for(int index = 5; index < args.size(); ++index) {
    bool valid = false;
    const auto value = args[index].toULongLong(&valid);
    if(!valid || args[index].startsWith('-')) return 2;
    indices.push_back(size_t(value));
  }
  const auto plane = read_metrics_plane(args[2], args[3], indices);
  if(!plane.error.isEmpty()) { QTextStream(stderr) << plane.error << '\n'; return 3; }
  const auto entropy = nc_texture_entropy(plane.plane.values, plane.plane.rows, plane.plane.cols);
  if(!entropy.valid) { QTextStream(stderr) << entropy.error << '\n'; return 3; }
  if(QFileInfo::exists(args[4])) { QTextStream(stderr) << "Output already exists\n"; return 3; }
  QSaveFile output(args[4]);
  if(!output.open(QIODevice::WriteOnly)) { QTextStream(stderr) << output.errorString() << '\n'; return 3; }
  QTextStream text(&output); text.setRealNumberPrecision(17);
  text << "row,col,entropy_bits,block_row,block_col,block_entropy_bits\n";
  for(size_t row = 0; row < entropy.rows; ++row)
    for(size_t col = 0; col < entropy.cols; ++col)
      text << row << ',' << col << ',' << entropy.values[row * entropy.cols + col] << ','
           << row / 4 << ',' << col / 4 << ','
           << entropy.block_entropy[(row / 4) * entropy.block_cols + col / 4] << '\n';
  text.flush();
  if(text.status() != QTextStream::Ok || !output.commit()) { QTextStream(stderr) << output.errorString() << '\n'; return 3; }
  print_text(QString("rows: %1\ncols: %2\nblock_rows: %3\nblock_cols: %4\nvalid_count: %5\nexcluded_count: %6\nvalid_blocks: %7\nminimum: %8\nmaximum: %9\nunits: bits\nblock_size: 4x4\nbins: 256\n")
    .arg(entropy.rows).arg(entropy.cols).arg(entropy.block_rows).arg(entropy.block_cols)
    .arg(entropy.valid_count).arg(entropy.excluded_count).arg(entropy.valid_blocks)
    .arg(entropy.source_minimum, 0, 'g', 17).arg(entropy.source_maximum, 0, 'g', 17));
  return 0;
}

int command_entropy(const QStringList &args) {
  bool ok = true;
  const int bins = args.size() > 4 ? args[4].toInt(&ok) : 256;
  if(!ok || bins < 2 || bins > 65536) return 2;
  std::vector<size_t> indices;
  for(int i = 5; i < args.size(); ++i) {
    const auto index = args[i].toULongLong(&ok);
    if(!ok || args[i].startsWith('-')) return 2;
    indices.push_back(size_t(index));
  }
  const auto plane=read_metrics_plane(args[2],args[3],indices);
  if(!plane.error.isEmpty()) { QTextStream(stderr) << plane.error << '\n'; return 3; }
  const auto entropy = nc_gray_entropy(plane.plane.values, bins);
  if(!entropy.valid) { QTextStream(stderr) << entropy.error << '\n'; return 3; }
  print_text(QString("valid_count: %1\nmissing_count: %2\nbins: %3\nminimum: %4\nmaximum: %5\nentropy_bits: %6\nnormalized_entropy: %7\n")
    .arg(entropy.valid_count).arg(entropy.missing_count).arg(entropy.bins)
    .arg(entropy.minimum,0,'g',15).arg(entropy.maximum,0,'g',15)
    .arg(entropy.entropy_bits,0,'g',15).arg(entropy.normalized_entropy,0,'g',15));
  return 0;
}

int command_hist(const QStringList &args) {
  QTextStream err(stderr);
  // hdf_cli hist <file> <path> [keep_fraction] [bins] [index ...]
  bool ok = true;
  const double keep = args.size() > 4 ? args[4].toDouble(&ok) : 0.05;
  if(!ok) { print_usage(); return 2; }
  const int bins = args.size() > 5 ? args[5].toInt(&ok) : 32;
  if(!ok) { print_usage(); return 2; }
  if(!std::isfinite(keep) || keep <= 0 || keep > 1) {
    err << "The sampling fraction must be greater than 0 and at most 1\n"; return 2;
  }
  if(bins < 1) { err << "The number of bins must be at least 1\n"; return 2; }
  std::vector<size_t> indices;
  for(int i = 6; i < args.size(); ++i) {
    const qulonglong index = args[i].toULongLong(&ok);
    if(!ok) { print_usage(); return 2; }
    indices.push_back(size_t(index));
  }
  NcReader shape_reader(args[2]);
  int group = -1, variable = -1;
  if(!shape_reader.locate(args[3].toStdString(), group, variable)) {
    err << "Variable not found\n"; return 3;
  }
  int rank = 0;
  if(nc_inq_varndims(group, variable, &rank) != NC_NOERR || rank < 2) {
    err << "A histogram needs a variable with at least two dimensions\n"; return 3;
  }
  std::vector<hsize_t> dims(size_t(rank), 0);
  std::vector<int> dimids(size_t(rank), 0);
  if(nc_inq_vardimid(group, variable, dimids.data()) != NC_NOERR) { err << "Cannot read dimensions\n"; return 3; }
  for(int i = 0; i < rank; ++i) {
    size_t length = 0;
    if(nc_inq_dimlen(group, dimids[size_t(i)], &length) != NC_NOERR) { err << "Cannot read dimensions\n"; return 3; }
    dims[size_t(i)] = hsize_t(length);
  }
  const NcPlaneResult plane = nc_read_plane_decoded(args[2], args[3].toStdString(), dims, indices);
  if(!plane.error.isEmpty()) { err << plane.error << '\n'; return 3; }
  const auto histogram = nc_histogram(plane.plane.values, keep, bins);
  print_text(nc_histogram_lines(histogram).join('\n') + "\n");
  return histogram.valid ? 0 : 3;
}
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  const QString command = args.value(1);
  if(command=="cmap" && args.size()<=4) {
    QString error;const auto palettes=HdfColorMap::load_registry(error);
    if(!error.isEmpty()){QTextStream(stderr)<<error;return 3;}
    bool found=args.size()==2;
    for(const auto &palette:palettes) {
      if(args.size()>2&&palette.id!=args[2]) continue;
      found=true;
      if(args.size()==4) {
        bool ok=false;const double position=args[3].toDouble(&ok);if(!ok||!std::isfinite(position)) return 2;
        const uint32_t rgb=HdfColorMap::sample(palette,position);
        print_text(QString("%1,%2,%3,%4\n").arg((rgb>>16)&255).arg((rgb>>8)&255).arg(rgb&255).arg(rgb>>24));
      } else print_text(QString("%1\t%2\t%3\n").arg(palette.id).arg(palette.colors.size()).arg(palette.license));
    }
    return found?0:3;
  }
  if(command=="rgb"&&args.size()==4) {
    if(args[3]!="unit"&&args[3]!="byte") return 2;
    QFile file(args[2]);if(!file.open(QIODevice::ReadOnly)||file.size()>2*1024*1024) return 3;
    HdfColorMap::Palette palette;
    const auto error=HdfColorMap::parse_rgb(file.readAll(),args[3]=="unit"?HdfColorMap::RgbScale::Unit:HdfColorMap::RgbScale::Byte,palette);
    if(!error.isEmpty()){QTextStream(stderr)<<error;return 3;}
    print_text(QString("colors: %1\nsha256: %2\n").arg(palette.colors.size()).arg(palette.source_hash));return 0;
  }
  if(command == "probe" && args.size() == 3) return command_probe(args[2]);
  if(command == "info" && args.size() == 3) return command_info(args[2]);
  if(command == "props" && args.size() == 4) return command_props(args[2], args[3]);
  if(command == "export" && (args.size() == 4 || args.size() == 5))
    return command_export(args[2], args[3], args.value(4));
  if(command == "csv" && args.size() >= 5) return command_csv(args);
  if(command == "mask" && args.size() >= 6) return command_mask(args);
  if(command == "hist" && args.size() >= 4) return command_hist(args);
  if(command == "entropy" && args.size() >= 4) return command_entropy(args);
  if(command == "texture-entropy" && args.size() >= 5) return command_texture_entropy(args);
  if(command == "corr" && args.size() >= 6) return command_corr(args);
  print_usage();
  return 2;
}

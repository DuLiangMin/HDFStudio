#include "slice_io.hpp"
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QSaveFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QRegularExpression>
#include <QLocale>
#include <QDateTime>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

QJsonObject slice_stamp(const QString &file){QFileInfo info(file);return {{"size_bytes",QString::number(info.size())},{"mtime_utc_ms",QString::number(info.lastModified().toMSecsSinceEpoch())}};}
QJsonObject slice_axis_json(const SliceAxisDescriptor &axis) {
  return {{"dimension_index",int(axis.dimension_index)},{"length",QString::number(axis.length)},{"dimension_name",axis.dimension_name},
    {"coordinate_path",axis.coordinate_path},{"units",axis.units},{"calendar",axis.calendar},{"standard_name",axis.standard_name},
    {"role",int(axis.role)},{"association",axis.association},{"diagnostic",axis.diagnostic},{"calendar_source",axis.calendar_source},{"date_supported",axis.date_supported}};
}
SliceAxisDescriptor slice_axis_from_json(const QJsonObject &object) {
  SliceAxisDescriptor axis;axis.dimension_index=object["dimension_index"].toInt();axis.length=object["length"].toString().toULongLong();
  axis.dimension_name=object["dimension_name"].toString();axis.coordinate_path=object["coordinate_path"].toString();axis.units=object["units"].toString();
  axis.calendar=object["calendar"].toString();axis.standard_name=object["standard_name"].toString();axis.role=SliceAxisRole(object["role"].toInt());
  axis.association=object["association"].toString();axis.diagnostic=object["diagnostic"].toString();axis.calendar_source=object["calendar_source"].toString();axis.date_supported=object["date_supported"].toBool();return axis;
}
QJsonObject slice_metadata(const QString &file,const QString &variable,const SliceInfo &info,const NcSliceSpec &spec) {
  QJsonArray shape,axes,fixed;
  for(auto length:info.shape)shape.append(QString::number(length));
  for(const auto &axis:info.axes)axes.append(slice_axis_json(axis));
  for(auto index:spec.fixed_indices)fixed.append(QString::number(index));
  return {{"schema",1},{"file",QFileInfo(file).absoluteFilePath()},{"variable",variable},{"source_stamp",slice_stamp(file)},
    {"shape",shape},{"axes",axes},{"spec",QJsonObject{{"row_axis",int(spec.row_axis)},{"col_axis",int(spec.col_axis)},{"fixed_indices",fixed}}},
    {"value_units",info.units},{"decode_profile","DecodeProfileV1"},{"index_spacing",true},{"flip_h",false},{"flip_v",false},
    {"request_id","0"},{"render_revision","0"},{"mask",QJsonValue::Null},{"storage_type",info.storage_type},{"netcdf_identity",info.netcdf_identity}};
}
bool slice_same_file(const QString &left,const QString &right) {
  if(QFileInfo(left).absoluteFilePath().compare(QFileInfo(right).absoluteFilePath(),
#ifdef Q_OS_WIN
     Qt::CaseInsensitive
#else
     Qt::CaseSensitive
#endif
     )==0)return true;
  if(!QFileInfo(left).exists()||!QFileInfo(right).exists())return false;
#ifdef Q_OS_WIN
  auto open=[](const QString &path){return CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);};
  HANDLE first=open(left),second=open(right);BY_HANDLE_FILE_INFORMATION a{},b{};
  bool same=first!=INVALID_HANDLE_VALUE&&second!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(first,&a)&&GetFileInformationByHandle(second,&b)&&a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
  if(first!=INVALID_HANDLE_VALUE)CloseHandle(first);if(second!=INVALID_HANDLE_VALUE)CloseHandle(second);return same;
#else
  struct stat a{},b{};return stat(QFile::encodeName(left).constData(),&a)==0&&stat(QFile::encodeName(right).constData(),&b)==0&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;
#endif
}
QString slice_write_atomic(const QString &path,bool overwrite,const std::function<QString(QIODevice&)> &writer) {
  if(overwrite){QSaveFile output(path);if(!output.open(QIODevice::WriteOnly))return output.errorString();const auto error=writer(output);if(!error.isEmpty()){output.cancelWriting();return error;}return output.commit()?QString():output.errorString();}
  QTemporaryFile output(QFileInfo(path).absolutePath()+"/tmp-hovmoller-export-XXXXXX");
  if(!output.open())return output.errorString();const auto error=writer(output);if(!error.isEmpty())return error;
  if(!output.flush())return output.errorString();output.close();
  if(!output.rename(path))return "Output exists or cannot be committed without overwriting";
  output.setAutoRemove(false);return {};
}
QString slice_write_csv(const QString &path,const NcPlane &plane,const std::vector<double> &original,
    const std::vector<double> &row_coordinates,const std::vector<double> &col_coordinates,const QJsonObject &metadata,bool overwrite) {
  if(plane.values.size()!=plane.rows*plane.cols||original.size()!=plane.values.size())return "Incomplete plane";
  if(slice_same_file(path,metadata["file"].toString()))return "Cannot overwrite a source dataset";
  return slice_write_atomic(path,overwrite,[&](QIODevice &output){
    QTextStream stream(&output);stream.setLocale(QLocale::c());stream.setRealNumberPrecision(17);
    stream<<"# hovmoller_schema=1\n# metadata="<<QJsonDocument(metadata).toJson(QJsonDocument::Compact)<<"\nrow,col,row_coord,col_coord,value,state\n";
    auto number=[](double value){return std::isfinite(value)?QString::number(value,'g',17):QString("nan");};
    for(size_t row=0;row<plane.rows;++row)for(size_t col=0;col<plane.cols;++col){const size_t i=row*plane.cols+col;const double value=plane.values[i];stream<<row<<','<<col<<',';
      if(row<row_coordinates.size())stream<<number(row_coordinates[row]);stream<<',';if(col<col_coordinates.size())stream<<number(col_coordinates[col]);
      stream<<','<<number(value)<<','<<(std::isfinite(value)?"valid":std::isfinite(original[i])?"masked":"missing")<<'\n';}
    stream.flush();return stream.status()==QTextStream::Ok?QString():QString("CSV write failed");
  });
}
int slice_command(const QStringList &args) {
  if(args.size()<7)return 2;
  const auto info=slice_inspect(args[2],args[3].toStdString());if(!info.error.isEmpty()){QTextStream(stderr)<<info.error;return 3;}
  if(args.size()!=7+int(info.shape.size()))return 2;
  NcSliceSpec spec;auto parse=[](const QString &text,size_t &value){bool ok=false;if(!QRegularExpression("^[0-9]+$").match(text).hasMatch())return false;value=text.toULongLong(&ok);return ok;};
  if(!parse(args[4],spec.row_axis)||!parse(args[5],spec.col_axis))return 2;
  spec.fixed_indices.resize(info.shape.size());for(size_t axis=0;axis<info.shape.size();++axis)if(!parse(args[7+int(axis)],spec.fixed_indices[axis]))return 2;
  auto read=nc_read_slice_decoded(args[2],args[3].toStdString(),info.shape,spec);if(!read.error.isEmpty()){QTextStream(stderr)<<read.error_code<<": "<<read.error;return 3;}
  QString error;auto rows=slice_read_coordinate(args[2],info.axes[spec.row_axis],error);auto cols=slice_read_coordinate(args[2],info.axes[spec.col_axis],error);
  spec.fixed_indices[spec.row_axis]=0;spec.fixed_indices[spec.col_axis]=0;
  auto metadata=slice_metadata(args[2],args[3],info,spec);QJsonArray fixed;
  for(size_t axis=0;axis<info.shape.size();++axis)if(axis!=spec.row_axis&&axis!=spec.col_axis){auto value=slice_read_coordinate(args[2],info.axes[axis],error,spec.fixed_indices[axis]);fixed.append(QJsonObject{{"dimension_index",int(axis)},{"index0",QString::number(spec.fixed_indices[axis])},{"value",value.empty()||!std::isfinite(value[0])?QJsonValue(QJsonValue::Null):QJsonValue(value[0])},{"units",info.axes[axis].units}});}
  metadata["fixed_coordinates"]=fixed;metadata["precision_warning"]=read.precision_warning;metadata["reader"]=read.reader;
  error=slice_write_csv(args[6],read.plane,read.plane.values,rows,cols,metadata);
  if(!error.isEmpty()){QTextStream(stderr)<<error;return 4;}return 0;
}

// 小JSON控制消息、固定版本二进制平面。每次命令只处理一个只读请求。
#include "slice_worker.hpp"
#include "grib/grib_data.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDataStream>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
bool integer(const QJsonValue &value,quint64 &number) {
  if(!value.isString()||!QRegularExpression("^[0-9]+$").match(value.toString()).hasMatch())return false;
  bool valid=false;number=value.toString().toULongLong(&valid);return valid;
}
QString json_write(const QString &path,const QJsonObject &object) {
  return slice_write_atomic(path,false,[&](QIODevice &file){auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);if(bytes.size()>4*1024*1024)return QString("JSON exceeds 4 MiB protocol limit");return file.write(bytes)==bytes.size()?QString():QString("JSON write failed");});
}
bool read_json(const QString &path,QJsonObject &object) {
  QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>4*1024*1024)return false;
  QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())return false;object=doc.object();return true;
}
QString binary_write(const QString &path,const std::vector<double> &values,size_t rows,size_t cols,quint64 id,bool header,quint32 version=1) {
  return slice_write_atomic(path,false,[&](QIODevice &file){QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    if(header){stream.writeRawData("HDFSLICE",8);stream<<version<<quint32(0)<<quint64(rows)<<quint64(cols)<<id<<quint64(values.size()*8);}
    for(double value:values)stream<<value;return stream.status()==QDataStream::Ok?QString():QString("Binary write failed");});
}
}
int slice_worker(const QString &request_path) {
  QElapsedTimer total_timer;total_timer.start();
  const QString directory=QFileInfo(request_path).absolutePath();QJsonObject request;
  if(!read_json(request_path,request))return 2;
  quint64 request_id=0,source_revision=0;
  QJsonObject result{{"schema",1},{"request_id",request["request_id"]},{"source_revision",request["source_revision"]},{"status","error"},
    {"error_code","ProtocolError"},{"message","Invalid request"},{"reader",""},{"actual_shape",QJsonValue::Null},{"axes",QJsonValue::Null},{"spec",QJsonValue::Null},{"diagnostics",QJsonArray()}};
  auto finish=[&](int code,const QString &error_code,const QString &message){result["error_code"]=error_code;result["message"]=message;if(!json_write(directory+"/result.json",result).isEmpty())return 4;return code;};
  const bool grib_request=request["schema"].toDouble()==2&&request["source_kind"]=="grib"&&request["decode_profile"]=="GribDecodedV1";
  if((request["schema"].toDouble()!=1&&!grib_request)||!integer(request["request_id"],request_id)||!integer(request["source_revision"],source_revision))return finish(2,"ProtocolError","Invalid version, source kind, profile or request id");
  if(grib_request){result["schema"]=2;result["source_kind"]="grib";result["decode_profile"]="GribDecodedV1";}
  const QString file=request["file"].toString(),variable=request["variable"].toString(),operation=request["operation"].toString();
  if(!QFileInfo(file).isAbsolute()||!variable.startsWith('/')||(operation!="inspect"&&operation!="read"))return finish(2,"ProtocolError","Invalid file, variable or operation");
  if(request["source_stamp"].toObject()!=slice_stamp(file))return finish(3,"SourceChanged","Source file changed before read");
  if(GribData::is_grib(file)!=grib_request)return finish(2,"ProtocolError","Source format does not match protocol envelope");
  auto info=slice_inspect(file,variable.toStdString());if(!info.error.isEmpty())return finish(3,info.error_code,info.error);
  QJsonArray axes,shape;for(const auto &axis:info.axes)axes.append(slice_axis_json(axis));for(auto n:info.shape)shape.append(QString::number(n));
  result["axes"]=axes;result["actual_shape"]=shape;result["reader"]=info.reader;result["value_units"]=info.units;result["storage_type"]=info.storage_type;result["netcdf_identity"]=info.netcdf_identity;result["chunk_bytes"]=QString::number(info.chunk_bytes);
  if(grib_request){result["source_working_bytes"]=QString::number(info.source_working_bytes);result["diagnostics"]=QJsonArray::fromStringList(info.diagnostics);result["timing_ms"]=QJsonObject{{"catalog_scan",double(info.catalog_scan_ms)},{"worker_total",double(total_timer.elapsed())}};}
  if(operation=="inspect") {if(request["source_stamp"].toObject()!=slice_stamp(file))return finish(3,"SourceChanged","Source changed during inspection");result["status"]="ok";return finish(0,"","");}
  if(request["expected_shape"].toArray()!=shape)return finish(3,"ShapeChanged","Variable shape changed");
  const auto object=request["spec"].toObject();NcSliceSpec spec;
  if(!object["row_axis"].isDouble()||!object["col_axis"].isDouble()||object["row_axis"].toDouble()!=object["row_axis"].toInt()||object["col_axis"].toDouble()!=object["col_axis"].toInt()||object["row_axis"].toInt()<0||object["col_axis"].toInt()<0)return finish(2,"ProtocolError","Invalid axes");
  spec.row_axis=object["row_axis"].toInt();spec.col_axis=object["col_axis"].toInt();
  for(const auto &value:object["fixed_indices"].toArray()){quint64 n=0;if(!integer(value,n))return finish(2,"ProtocolError","Invalid fixed index");spec.fixed_indices.push_back(size_t(n));}
  const auto validation=slice_validate(std::vector<size_t>(info.shape.begin(),info.shape.end()),spec);if(!validation.isEmpty())return finish(3,validation,"Invalid slice");
  spec.fixed_indices[spec.row_axis]=0;spec.fixed_indices[spec.col_axis]=0;
  std::vector<size_t> coordinate_axes;
  if(!request["coordinate_axes"].isArray())return finish(2,"ProtocolError","Missing coordinate_axes");
  for(const auto &value:request["coordinate_axes"].toArray()) {
    if(!value.isDouble()||value.toDouble()!=value.toInt()||value.toInt()<0||value.toInt()>=int(info.shape.size()))return finish(2,"ProtocolError","Invalid coordinate axis");
    size_t axis=value.toInt();if(std::find(coordinate_axes.begin(),coordinate_axes.end(),axis)!=coordinate_axes.end())return finish(2,"ProtocolError","Repeated coordinate axis");
    if(axis!=spec.row_axis&&axis!=spec.col_axis&&info.shape[axis]>1048576)return finish(5,"MemoryBudgetExceeded","Fixed coordinate vector exceeds limit");coordinate_axes.push_back(axis);
  }
  if(std::find(coordinate_axes.begin(),coordinate_axes.end(),spec.row_axis)==coordinate_axes.end()||std::find(coordinate_axes.begin(),coordinate_axes.end(),spec.col_axis)==coordinate_axes.end())return finish(2,"ProtocolError","Display coordinates must be requested");
  QStorageInfo storage(directory);const quint64 pixels=info.shape[spec.row_axis]*info.shape[spec.col_axis];
  quint64 coordinate_bytes=0;for(auto axis:coordinate_axes)coordinate_bytes+=info.shape[axis]*8;
  if(storage.isValid()&&storage.bytesAvailable()<qint64(pixels*8+coordinate_bytes+4*1024*1024))return finish(4,"OutputError","Insufficient temporary disk space");
  QElapsedTimer stage_timer;stage_timer.start();const auto read=nc_read_slice_decoded(file,variable.toStdString(),info.shape,spec);const auto read_ms=stage_timer.elapsed();stage_timer.restart();if(!read.error.isEmpty())return finish(3,read.error_code,read.error);
  QString error=binary_write(directory+"/plane.bin",read.plane.values,read.plane.rows,read.plane.cols,request_id,true,grib_request?2:1);if(!error.isEmpty())return finish(4,"OutputError",error);
  QJsonArray files,fixed,diagnostics;
  for(const auto &note:info.diagnostics)diagnostics.append(note);
  for(size_t axis=0;axis<info.axes.size();++axis) {
    if(info.axes[axis].coordinate_path.isEmpty())continue;
    // Fixed coordinates are bounded; very long axes use a scalar read below.
    std::vector<double> values;
    const bool full=std::find(coordinate_axes.begin(),coordinate_axes.end(),axis)!=coordinate_axes.end();
    if(full)values=slice_read_coordinate(file,info.axes[axis],error);
    else values=slice_read_coordinate(file,info.axes[axis],error,spec.fixed_indices[axis]);
    if(!error.isEmpty()){diagnostics.append(error);info.axes[axis].diagnostic+=error;error.clear();continue;}
    if(full&&!values.empty()) {
      bool missing=false,repeated=false,descending=false,nonuniform=false,seam=false;double first_delta=NAN;
      for(size_t i=0;i<values.size();++i){if(!std::isfinite(values[i]))missing=true;if(i&&std::isfinite(values[i])&&std::isfinite(values[i-1])){const double delta=values[i]-values[i-1];repeated|=delta==0;descending|=delta<0;if(std::isfinite(first_delta)){nonuniform|=std::abs(delta-first_delta)>1e-9*std::max(std::abs(delta),std::abs(first_delta));}else first_delta=delta;if(info.axes[axis].role==SliceAxisRole::Longitude&&std::abs(delta)>180)seam=true;}}
      QStringList notes;if(missing)notes<<"missing coordinates";if(repeated)notes<<"repeated coordinates";if(descending)notes<<"descending segments (source order retained)";if(nonuniform)notes<<"nonuniform spacing";if(seam)notes<<"longitude seam (no wrapping)";
      if(!notes.isEmpty()){if(!info.axes[axis].diagnostic.isEmpty())info.axes[axis].diagnostic+="; ";info.axes[axis].diagnostic+=notes.join("; ");}
    }
    if(full&&!values.empty()){const QString name=QString("axis-%1.f64").arg(axis);error=binary_write(directory+"/"+name,values,0,0,request_id,false);if(!error.isEmpty())return finish(4,"OutputError",error);files.append(QJsonObject{{"dimension_index",int(axis)},{"file",name},{"length",QString::number(values.size())},{"bytes",QString::number(values.size()*8)}});}
    if(axis!=spec.row_axis&&axis!=spec.col_axis){double value=!full&&!values.empty()?values[0]:spec.fixed_indices[axis]<values.size()?values[spec.fixed_indices[axis]]:NAN;fixed.append(QJsonObject{{"dimension_index",int(axis)},{"index0",QString::number(spec.fixed_indices[axis])},{"value",std::isfinite(value)?QJsonValue(value):QJsonValue(QJsonValue::Null)},{"units",info.axes[axis].units}});}
  }
  if(request["source_stamp"].toObject()!=slice_stamp(file))return finish(3,"SourceChanged","Source changed during read");
  result["status"]="ok";result["spec"]=slice_metadata(file,variable,info,spec)["spec"];result["rows"]=QString::number(read.plane.rows);result["cols"]=QString::number(read.plane.cols);
  result["decode_profile"]=info.decode_profile;result["precision_warning"]=read.precision_warning;result["data_file"]="plane.bin";result["data_bytes"]=QString::number(48+read.plane.values.size()*8);
  if(grib_request)result["missing_fields"]=QString::number(read.missing_fields);
  axes={};for(const auto &axis:info.axes)axes.append(slice_axis_json(axis));result["axes"]=axes;
  result["timing_ms"]=QJsonObject{{"read_decode",double(read_ms)},{"transfer_and_coordinates",double(stage_timer.elapsed())},{"worker_total",double(total_timer.elapsed())}};
  if(grib_request){auto timing=result["timing_ms"].toObject();timing["catalog_scan"]=double(info.catalog_scan_ms);result["timing_ms"]=timing;}
  result["coordinate_files"]=files;result["fixed_coordinates"]=fixed;result["diagnostics"]=diagnostics;return finish(0,"","");
}
SliceWorkerData slice_load_result(const QString &directory,quint64 request_id) {
  SliceWorkerData out;QJsonObject result;
  auto fail=[&](const QString &message){out.error=message;out.plane={};return out;};
  if(!read_json(directory+"/result.json",result))return fail("ProtocolError: invalid result JSON");
  const bool grib_result=result["schema"].toDouble()==2&&result["source_kind"]=="grib"&&result["decode_profile"]=="GribDecodedV1";
  quint64 received=0,revision=0;if((result["schema"].toDouble()!=1&&!grib_result)||!integer(result["request_id"],received)||received!=request_id||!integer(result["source_revision"],revision))return fail("ProtocolError: invalid result envelope");
  if(result["schema"].toDouble()==1&&result["source_kind"]=="grib")return fail("ProtocolError: GRIB requires schema 2");
  out.metadata=result;if(result["status"]!="ok")return fail(result["error_code"].toString()+": "+result["message"].toString());
  for(const auto &value:result["actual_shape"].toArray()){quint64 n;if(!integer(value,n)||!n)return fail("ProtocolError: invalid shape");out.info.shape.push_back(n);}
  for(const auto &value:result["axes"].toArray())out.info.axes.push_back(slice_axis_from_json(value.toObject()));
  if(out.info.shape.size()<2||out.info.axes.size()!=out.info.shape.size())return fail("ProtocolError: axis count");
  for(size_t i=0;i<out.info.axes.size();++i)if(out.info.axes[i].dimension_index!=i||out.info.axes[i].length!=out.info.shape[i])return fail("ProtocolError: inconsistent axes");
  if(grib_result){out.info.source_kind="grib";out.info.decode_profile="GribDecodedV1";if(!integer(result["source_working_bytes"],out.info.source_working_bytes)||out.info.source_working_bytes>512ull*1024*1024)return fail("ProtocolError: invalid source working set");
    for(const auto &value:result["axes"].toArray()) {
      const QJsonObject axis=value.toObject();const QJsonValue labels=axis.value("source_labels");
      if(labels.isUndefined())continue;
      if(!labels.isArray()||quint64(labels.toArray().size())!=out.info.shape[axis["dimension_index"].toInt()])return fail("ProtocolError: invalid source labels");
      for(const auto &label:labels.toArray())if(!label.isString())return fail("ProtocolError: non-string source label");
    }
  }
  out.info.units=result["value_units"].toString();out.info.reader=result["reader"].toString();out.info.storage_type=result["storage_type"].toString();out.info.netcdf_identity=result["netcdf_identity"].toBool();if(!integer(result["chunk_bytes"],out.info.chunk_bytes))return fail("ProtocolError: invalid chunk byte size");
  if(!result.contains("data_file")){if(!result["spec"].isNull())return fail("ProtocolError: inspection spec must be null");return out;}
  if(result["decode_profile"]!=out.info.decode_profile||!result["coordinate_files"].isArray()||!result["fixed_coordinates"].isArray()||!result["precision_warning"].isBool())return fail("ProtocolError: incomplete read metadata");
  quint64 missing_fields=0;if(grib_result&&!integer(result["missing_fields"],missing_fields))return fail("ProtocolError: invalid missing-field count");
  const auto spec=result["spec"].toObject();out.spec.row_axis=spec["row_axis"].toInt(-1);out.spec.col_axis=spec["col_axis"].toInt(-1);
  if(!spec["row_axis"].isDouble()||!spec["col_axis"].isDouble()||spec["row_axis"].toDouble()!=spec["row_axis"].toInt(-1)||spec["col_axis"].toDouble()!=spec["col_axis"].toInt(-1)||!spec["fixed_indices"].isArray())return fail("ProtocolError: invalid result axes");
  for(const auto &value:spec["fixed_indices"].toArray()){quint64 n;if(!integer(value,n))return fail("ProtocolError: fixed index");out.spec.fixed_indices.push_back(n);}
  const auto validation=slice_validate(std::vector<size_t>(out.info.shape.begin(),out.info.shape.end()),out.spec);if(!validation.isEmpty())return fail("ProtocolError: invalid result slice");
  quint64 rows=0,cols=0,total=0;if(!integer(result["rows"],rows)||!integer(result["cols"],cols)||!integer(result["data_bytes"],total)||rows!=out.info.shape[out.spec.row_axis]||cols!=out.info.shape[out.spec.col_axis]||total!=48+rows*cols*8||result["data_file"]!="plane.bin")return fail("ProtocolError: plane dimensions");
  QFile file(directory+"/plane.bin");if(!file.open(QIODevice::ReadOnly)||quint64(file.size())!=total)return fail("ProtocolError: truncated or oversized plane");
  QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
  char magic[8];quint32 version,flags;quint64 r,c,id,bytes;stream.readRawData(magic,8);stream>>version>>flags>>r>>c>>id>>bytes;
  if(std::memcmp(magic,"HDFSLICE",8)||version!=(grib_result?2u:1u)||flags!=0||r!=rows||c!=cols||id!=request_id||bytes!=rows*cols*8)return fail("ProtocolError: invalid plane header");
  out.plane.rows=rows;out.plane.cols=cols;out.plane.values.resize(rows*cols);for(double &value:out.plane.values)stream>>value;
  if(stream.status()!=QDataStream::Ok)return fail("ProtocolError: plane payload");
  for(const auto &value:result["coordinate_files"].toArray()) {
    const auto object=value.toObject();const int axis=object["dimension_index"].toInt(-1);quint64 length=0,bytes=0;
    if(!object["dimension_index"].isDouble()||object["dimension_index"].toDouble()!=axis||axis<0||axis>=int(out.info.shape.size())||!integer(object["length"],length)||!integer(object["bytes"],bytes)||length!=out.info.shape[axis]||length>16ull*1024*1024||(axis!=int(out.spec.row_axis)&&axis!=int(out.spec.col_axis)&&length>1048576)||bytes!=length*8||object["file"]!=QString("axis-%1.f64").arg(axis)||out.coordinates.count(axis))return fail("ProtocolError: coordinate metadata");
    QFile coordinate(directory+"/"+object["file"].toString());if(!coordinate.open(QIODevice::ReadOnly)||quint64(coordinate.size())!=bytes)return fail("ProtocolError: coordinate size");
    QDataStream data(&coordinate);data.setByteOrder(QDataStream::LittleEndian);data.setFloatingPointPrecision(QDataStream::DoublePrecision);auto &values=out.coordinates[axis];values.resize(length);for(double &number:values)data>>number;if(data.status()!=QDataStream::Ok)return fail("ProtocolError: coordinate payload");
  }
  return out;
}

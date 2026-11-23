// Immutable catalogs are cached per source stamp; ordinary layer changes never rescan a file.
#include "grib_data.hpp"
#include <QFileInfo>
#include <QCryptographicHash>
#include <mutex>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <climits>

namespace {
std::mutex catalog_mutex;
QMap<QString,std::shared_ptr<const grib::Catalog>> catalogs;
QString canonical(const QString& file) { return QFileInfo(file).absoluteFilePath(); }
const grib::Cube* cube_of(const grib::Catalog& catalog,const QString& target) {
  for(const auto& cube:catalog.cubes)if(cube.id==target)return &cube;
  return nullptr;
}
const grib::Field& representative(const grib::Catalog& catalog,const QString& target) {
  const auto* cube=cube_of(catalog,target);
  return catalog.fields.at(size_t(cube?cube->representative:grib::resolve(catalog,target)));
}
void check_stamp(const grib::Catalog& catalog) {
  const QFileInfo info(catalog.path);
  if(info.size()!=catalog.size||info.lastModified().toMSecsSinceEpoch()!=catalog.modified)
    throw grib::Error("SourceChanged","Source changed; close and reopen file");
}
}
namespace GribData {
bool is_grib(const QString& file) {
  QFile source(file);
  if(!source.open(QIODevice::ReadOnly))return false;
  const auto bytes=source.read(1024*1024);
  if(bytes.startsWith("CDF"))return false;
  const auto hdf=QByteArray::fromHex("894844460d0a1a0a");
  for(qint64 offset=0;offset<source.size();) {
    if(source.seek(offset)&&source.read(8)==hdf)return false;
    if(offset>0&&offset>source.size()/2)break;
    offset=offset==0?512:offset*2;
  }
  return bytes.contains("GRIB"); // Structural validation is performed by scan_file, including damaged GRIB.
}
std::shared_ptr<const grib::Catalog> open(const QString& file,const std::function<bool()>& cancelled) {
  const auto path=canonical(file);
  {
    std::lock_guard<std::mutex> lock(catalog_mutex);
    auto found=catalogs.find(path);
    if(found!=catalogs.end()){check_stamp(**found);return *found;}
  }
  const auto value=grib::scan_file(path,cancelled);
  if(cancelled&&cancelled())throw grib::Error("Cancelled","Indexing cancelled");
  std::lock_guard<std::mutex> lock(catalog_mutex);
  if(catalogs.size()>=16)throw grib::Error("ResourceLimit","At most 16 indexed GRIB files; close another file");
  catalogs[path]=value;
  return value;
}
void release(const QString& file) {std::lock_guard<std::mutex> lock(catalog_mutex);catalogs.remove(canonical(file));}
DataSource::ArrayInfo describe(const grib::Catalog& catalog,const QString& target) {
  check_stamp(catalog);
  DataSource::ArrayInfo info;
  const auto& field=representative(catalog,target);
  info.object={catalog.session,target,DataSource::NodeKind::Array};
  info.title=field.name;info.units=field.units;info.grid_kind=field.grid_type;info.reader="ecCodes";
  info.spectral=field.grid_type=="sh";
  info.rectangular=field.ni>0&&field.nj>0&&quint64(field.ni)*quint64(field.nj)==quint64(field.points)&&!info.spectral;
  if(const auto* cube=cube_of(catalog,target)) {
    info.title=cube->title;
    for(const auto& axis:cube->axes) {
      DataSource::AxisInfo item;item.name=axis.name;item.labels=axis.labels;item.length=size_t(axis.labels.size());
      info.shape.push_back(item.length);info.axes.push_back(std::move(item));
    }
  }
  if(info.rectangular) {
    info.shape.push_back(size_t(field.nj));info.shape.push_back(size_t(field.ni));
    info.axes.push_back({"grid_y","index0",{}, {},size_t(field.nj)});
    info.axes.push_back({"grid_x","index0",{}, {},size_t(field.ni)});
  } else {
    info.shape.push_back(size_t(field.points));
    info.axes.push_back({info.spectral?"coefficient":"point","index0",{}, {},size_t(field.points)});
    info.disabled_reasons["image"]="Non-rectangular source: inspect point/coefficient table; no interpolation";
  }
  info.can_image=info.rectangular;info.can_section=info.rectangular;
  return info;
}
DataSource::ArrayInfo describe(const QString& file,const QString& target) {
  try{return describe(*open(file),target);}catch(const grib::Error& error){DataSource::ArrayInfo info;info.error=error.message;info.error_code=error.code;return info;}
}
DataSource::PlaneData read(const grib::Catalog& catalog,const QString& target,const std::vector<size_t>& indices,bool coordinates,bool quantized) {
  DataSource::PlaneData result;result.reader="ecCodes";result.decode_profile="GribDecodedV1";result.source_revision=1;
  try {
    check_stamp(catalog);
    std::vector<int> selection;for(auto value:indices){if(value>INT_MAX)throw grib::Error("InvalidSelection","Index overflow");selection.push_back(int(value));}
    const auto field_index=grib::resolve(catalog,target,selection);
    const auto &field=catalog.fields.at(size_t(field_index));
    auto plane=grib::read_plane(catalog,field_index,coordinates,quantized);
    result.grid_identity=field.grid_hash;
    result.time_identity=field.time_key;
    result.rows=size_t(plane.rows);result.cols=size_t(plane.cols);result.units=plane.units;
    result.values=std::move(plane.values);result.stored_x=std::move(plane.stored);result.lease=std::move(plane.memory_lease);
    result.stored_description=plane.raw_description;
    result.stored_semantics=plane.exact_stored?DataSource::StoredSemantics::ReconstructedQuantized:DataSource::StoredSemantics::Unavailable;
    if(coordinates) {
      result.coordinates.latitudes=std::move(plane.latitudes);result.coordinates.longitudes=std::move(plane.longitudes);
      result.coordinates.kind=result.coordinates.latitudes.empty()?DataSource::CoordinateKind::None:plane.rectangular?DataSource::CoordinateKind::PerPoint:DataSource::CoordinateKind::PointList;
      result.coordinates.status=result.coordinates.kind==DataSource::CoordinateKind::None?"unavailable":"verified-ecCodes";
    }
    check_stamp(catalog);
  } catch(const grib::Error& error){result.values.clear();result.error=error.message;result.error_code=error.code;}
  return result;
}
DataSource::PlaneData read(const QString& file,const QString& target,const std::vector<size_t>& indices,bool coordinates,bool quantized) {
  try{return read(*open(file),target,indices,coordinates,quantized);}catch(const grib::Error& error){DataSource::PlaneData result;result.error=error.message;result.error_code=error.code;return result;}
}
quint64 slice_source_bytes(const grib::Catalog& catalog,const QString& target) {
  quint64 bytes=0;
  auto include=[&](int index){if(index<0)return;const auto &field=catalog.fields.at(size_t(index));
    if(field.points<=0||quint64(field.points)>512ull*1024*1024/128||field.length<0||quint64(field.length)>512ull*1024*1024)
      throw grib::Error("ResourceLimit","Source field exceeds section working-set limit");
    bytes=std::max(bytes,quint64(field.points)*128+quint64(field.length)*2);};
  if(const auto *cube=cube_of(catalog,target)){for(int index:cube->lookup)include(index);}
  else include(grib::resolve(catalog,target));
  return bytes;
}
DataSource::PlaneData slice(const QString& file,const QString& target,const DataSource::ReadSelection& selection) {
  DataSource::PlaneData result;result.reader="ecCodes";result.decode_profile="GribDecodedV1";
  try {
    const auto catalog=open(file);const auto info=describe(*catalog,target);const auto rank=info.shape.size();
    if(!info.rectangular)throw grib::Error("UnsupportedGrid","Sections require a rectangular source grid");
    if(selection.row_axis>=rank||selection.col_axis>=rank||selection.row_axis==selection.col_axis||selection.fixed_indices.size()!=rank)
      throw grib::Error("InvalidSpec","Invalid slice axes or fixed indices");
    for(size_t axis=0;axis<rank;++axis)if(axis!=selection.row_axis&&axis!=selection.col_axis&&selection.fixed_indices[axis]>=info.shape[axis])throw grib::Error("InvalidSpec","Fixed index outside axis");
    result.rows=info.shape[selection.row_axis];result.cols=info.shape[selection.col_axis];result.units=info.units;
    if(!result.rows||!result.cols||result.rows>16ull*1024*1024/result.cols)throw grib::Error("ResourceLimit","Slice exceeds 16 Mi samples");
    const quint64 output_bytes=quint64(result.rows)*result.cols*64;
    if(output_bytes+slice_source_bytes(*catalog,target)+16ull*1024*1024>512ull*1024*1024)
      throw grib::Error("ResourceLimit","Section and source decode exceed 512 MiB working-set reservation");
    result.lease=grib::reserve_memory(output_bytes);
    result.values.assign(result.rows*result.cols,grib::missing);
    const size_t leading=rank-2;
    std::vector<size_t> indices(selection.fixed_indices.begin(),selection.fixed_indices.begin()+leading);
    const bool row_leading=selection.row_axis<leading,col_leading=selection.col_axis<leading;
    for(size_t row=0;row<(row_leading?result.rows:1);++row)for(size_t col=0;col<(col_leading?result.cols:1);++col) {
      if(row_leading)indices[selection.row_axis]=row;if(col_leading)indices[selection.col_axis]=col;
      auto plane=read(*catalog,target,indices,false,false);
      if(plane.error_code=="MissingSlice"){++result.missing_fields;continue;}
      if(!plane.error.isEmpty())throw grib::Error(plane.error_code,plane.error);
      for(size_t r=row_leading?row:0;r<(row_leading?row+1:result.rows);++r)
        for(size_t c=col_leading?col:0;c<(col_leading?col+1:result.cols);++c) {
          size_t y=selection.fixed_indices[leading],x=selection.fixed_indices[leading+1];
          if(selection.row_axis==leading)y=r;if(selection.col_axis==leading)y=c;
          if(selection.row_axis==leading+1)x=r;if(selection.col_axis==leading+1)x=c;
          result.values[r*result.cols+c]=plane.values[y*plane.cols+x];
        }
    }
    check_stamp(*catalog);
  } catch(const grib::Error& error){result.values.clear();result.lease.reset();result.error=error.message;result.error_code=error.code;}
  return result;
}
QString properties(const QString& file,const QString& target) {
  try {
    const auto catalog=open(file);
    if(target=="/"||target.isEmpty())return QString("GRIB file: %1\nFormat: %2\nFields: %3\nVirtual variables: %4\nCF: not applicable\n%5").arg(catalog->path,catalog->format).arg(catalog->fields.size()).arg(catalog->cubes.size()).arg(catalog->warnings.join('\n'));
    QString result=grib::properties(*catalog,target);
    if(const auto* cube=cube_of(*catalog,target)) {
      result.prepend("Virtual variable; details below describe representative field, not every field.\n");
      QStringList varying;
      const auto& first=catalog->fields[size_t(cube->representative)];
      for(auto it=first.keys.cbegin();it!=first.keys.cend();++it) {
        for(int index:cube->lookup)if(index>=0&&catalog->fields[size_t(index)].keys.value(it.key())!=it.value()){varying<<it.key();break;}
      }
      result+="\nVarying keys: "+varying.join(", ")+"\nKeys may be encoded or computed ecCodes accessors.\n";
    }
    return result;
  }catch(const grib::Error& error){return error.code+": "+error.message;}
}
}

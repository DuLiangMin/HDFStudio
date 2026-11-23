#include "slice_coordinates.hpp"
#include "grib/grib_data.hpp"
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>
#include <QRegularExpression>
#include <QFile>
#include <QDir>
#include <limits>

namespace {
struct Handle {hid_t id;herr_t(*close)(hid_t);~Handle(){if(id>=0)close(id);}};
QString attr(int group,int variable,const char *name) {
  nc_type type;size_t count=0;if(nc_inq_att(group,variable,name,&type,&count)!=NC_NOERR)return {};
  if(type==NC_CHAR&&count<=65536){QByteArray text(int(count),'\0');if(nc_get_att_text(group,variable,name,text.data())==NC_NOERR)return QString::fromUtf8(text).trimmed();}
  if(type==NC_STRING&&count==1){char *value=nullptr;if(nc_get_att_string(group,variable,name,&value)==NC_NOERR){const auto text=QString::fromUtf8(value).trimmed();nc_free_string(1,&value);return text;}}
  return {};
}
QString group_path(int group,int variable) {
  size_t length=0;nc_inq_grpname_full(group,&length,nullptr);QByteArray path(int(length)+1,'\0');nc_inq_grpname_full(group,nullptr,path.data());char name[NC_MAX_NAME+1]={};nc_inq_varname(group,variable,name);
  QString result=QString::fromUtf8(path.constData());if(!result.endsWith('/'))result+='/';return result+QString::fromUtf8(name);
}
bool coordinate_matches(int group,int variable,int dimension) {
  int rank=0,dim=-1;nc_type type;
  return nc_inq_varndims(group,variable,&rank)==NC_NOERR&&rank==1&&nc_inq_vardimid(group,variable,&dim)==NC_NOERR&&dim==dimension&&nc_inq_vartype(group,variable,&type)==NC_NOERR&&type!=NC_CHAR&&type!=NC_STRING&&type<=NC_UINT64;
}
void classify(SliceAxisDescriptor &axis,int group,int variable) {
  axis.units=attr(group,variable,"units");axis.standard_name=attr(group,variable,"standard_name");const auto axis_attr=attr(group,variable,"axis");
  const QStringList lat={"degrees_north","degree_north","degree_N","degrees_N","degreeN","degreesN"};
  const QStringList lon={"degrees_east","degree_east","degree_E","degrees_E","degreeE","degreesE"};
  const bool time=QRegularExpression("^(seconds?|minutes?|hours?|days?|milliseconds?|microseconds?|sec|min|hr|s|d)\\s+since\\s+.+$").match(axis.units).hasMatch();
  if(lat.contains(axis.units))axis.role=SliceAxisRole::Latitude;else if(lon.contains(axis.units))axis.role=SliceAxisRole::Longitude;else if(time)axis.role=SliceAxisRole::Time;
  const bool conflict=(axis.role==SliceAxisRole::Latitude&&((!axis.standard_name.isEmpty()&&axis.standard_name!="latitude")||(!axis_attr.isEmpty()&&axis_attr!="Y")))||
    (axis.role==SliceAxisRole::Longitude&&((!axis.standard_name.isEmpty()&&axis.standard_name!="longitude")||(!axis_attr.isEmpty()&&axis_attr!="X")))||
    (axis.role==SliceAxisRole::Time&&((!axis.standard_name.isEmpty()&&axis.standard_name!="time")||(!axis_attr.isEmpty()&&axis_attr!="T")));
  if(conflict){axis.role=SliceAxisRole::Unknown;axis.diagnostic="Conflicting coordinate metadata";}
  if(time){axis.calendar=attr(group,variable,"calendar");axis.calendar_source=axis.calendar.isEmpty()?"CF_default":"declared";if(axis.calendar.isEmpty())axis.calendar="standard";
    const QStringList supported={"standard","gregorian","proleptic_gregorian","noleap","365_day","all_leap","366_day","360_day","julian"};axis.date_supported=supported.contains(axis.calendar);
    nc_type ignored;size_t length;for(const char *name:{"month_lengths","leap_year","leap_month"})if(nc_inq_att(group,variable,name,&ignored,&length)==NC_NOERR){axis.calendar_source="custom";axis.date_supported=false;axis.diagnostic="Custom calendar is not supported";}}
}
}
SliceInfo slice_inspect(const QString &file,const std::string &path) {
  SliceInfo out;
  if(GribData::is_grib(file)) {
    out.source_kind="grib";out.decode_profile="GribDecodedV1";out.reader="ecCodes";out.storage_type="GRIB decoded double";
    try {
      QElapsedTimer timer;timer.start();const auto catalog=GribData::open(file);out.catalog_scan_ms=timer.elapsed();
      const auto info=GribData::describe(*catalog,QString::fromUtf8(path.c_str()));
      if(!info.rectangular)throw grib::Error("UnsupportedGrid","Sections require a rectangular source grid; no interpolation");
      out.shape.assign(info.shape.begin(),info.shape.end());out.units=info.units;
      out.source_working_bytes=GribData::slice_source_bytes(*catalog,QString::fromUtf8(path.c_str()));
      for(size_t i=0;i<info.axes.size();++i){SliceAxisDescriptor axis;axis.dimension_index=i;axis.length=info.shape[i];axis.dimension_name=info.axes[i].name;
        axis.units="index0";axis.source_labels=info.axes[i].labels;axis.diagnostic="Source index order; physical coordinate semantics unverified";out.axes.push_back(std::move(axis));}
      out.diagnostics<<"GRIB section uses source indices and original axis labels; no CF coordinate inference";
    }catch(const grib::Error &error){out.error_code=error.code;out.error=error.message;}
    return out;
  }
  {NcReader reader(file);int group=-1,variable=-1,rank=0;
    if(reader.locate(path,group,variable)) {
      out.reader="netCDF-C";
      if(nc_inq_varndims(group,variable,&rank)!=NC_NOERR||rank<2){out.error="Variable requires at least two dimensions";out.error_code="InvalidSpec";return out;}
      nc_type dtype=NC_NAT;nc_inq_vartype(group,variable,&dtype);
      if(dtype<NC_BYTE||dtype>NC_UINT64||dtype==NC_CHAR||dtype==NC_STRING){out.error="Only fixed-width numeric scalar elements are supported";out.error_code="UnsupportedType";return out;}
      char type_name[NC_MAX_NAME+1]={};nc_inq_type(group,dtype,type_name,nullptr);out.storage_type=QString::fromUtf8(type_name);
      int storage=NC_CONTIGUOUS;std::vector<size_t> chunks(rank);size_t width=0;nc_inq_type(group,dtype,nullptr,&width);
      if(nc_inq_var_chunking(group,variable,&storage,chunks.data())==NC_NOERR&&storage==NC_CHUNKED){quint64 bytes=width;for(auto length:chunks){if(!length||bytes>std::numeric_limits<quint64>::max()/length){out.error="Chunk byte size overflow";out.error_code="MemoryBudgetExceeded";return out;}bytes*=length;}out.chunk_bytes=bytes;}
      QFile signature(file);if(signature.open(QIODevice::ReadOnly)){const auto bytes=signature.read(4);out.netcdf_identity=bytes.size()==4&&bytes.left(3)=="CDF"&&(bytes[3]==1||bytes[3]==2||bytes[3]==5);}
      if(!out.netcdf_identity){nc_type ignored;size_t length=0;out.netcdf_identity=nc_inq_att(reader.id,NC_GLOBAL,"_NCProperties",&ignored,&length)==NC_NOERR;}
      std::vector<int> ids(rank);nc_inq_vardimid(group,variable,ids.data());
      out.units=attr(group,variable,"units");
      for(int axis=0;axis<rank;++axis){char name[NC_MAX_NAME+1]={};size_t length=0;nc_inq_dim(group,ids[axis],name,&length);out.shape.push_back(length);SliceAxisDescriptor descriptor;descriptor.dimension_index=axis;descriptor.length=length;descriptor.dimension_name=QString::fromUtf8(name);
        if(!out.netcdf_identity){descriptor.dimension_name=QString("axis %1").arg(axis);descriptor.diagnostic="NetCDF metadata identity unverified; generic index slice";out.axes.push_back(descriptor);continue;}
        std::vector<std::pair<int,int>> candidates;int scope=group;
        while(scope>=0){int coordinate=-1;if(nc_inq_varid(scope,name,&coordinate)==NC_NOERR&&coordinate_matches(scope,coordinate,ids[axis])){candidates.push_back({scope,coordinate});descriptor.association="dimension_coordinate";break;}int parent=-1;if(nc_inq_grp_parent(scope,&parent)!=NC_NOERR)break;scope=parent;}
        if(candidates.empty())for(const auto &token:attr(group,variable,"coordinates").split(QRegularExpression("\\s+"),Qt::SkipEmptyParts)) {
          int cg=-1,cv=-1;bool found=false;
          if(token.startsWith('/'))found=reader.locate(token.toStdString(),cg,cv);
          else {int search=group;while(search>=0){int slash=token.lastIndexOf('/');cg=search;
            if(slash>=0){size_t length=0;nc_inq_grpname_full(search,&length,nullptr);QByteArray parent(int(length)+1,'\0');nc_inq_grpname_full(search,nullptr,parent.data());const auto gp=QDir::cleanPath(QString::fromUtf8(parent.constData())+"/"+token.left(slash)).toUtf8();if(nc_inq_grp_full_ncid(reader.id,gp.constData(),&cg)!=NC_NOERR)cg=-1;}
            const QByteArray vn=token.mid(slash+1).toUtf8();if(cg>=0&&nc_inq_varid(cg,vn.constData(),&cv)==NC_NOERR){found=true;break;}int parent=-1;if(nc_inq_grp_parent(search,&parent)!=NC_NOERR)break;search=parent;}}
          if(found&&coordinate_matches(cg,cv,ids[axis])&&std::find(candidates.begin(),candidates.end(),std::make_pair(cg,cv))==candidates.end())candidates.push_back({cg,cv});
        }
        if(candidates.size()==1){descriptor.coordinate_path=group_path(candidates[0].first,candidates[0].second);if(descriptor.association=="none")descriptor.association="coordinates";classify(descriptor,candidates[0].first,candidates[0].second);}
        else if(candidates.size()>1){descriptor.association="ambiguous";descriptor.diagnostic="Multiple coordinate candidates";}
        out.axes.push_back(descriptor);}
      return out;
    }
  }
  Handle fid{H5Fopen(nc_path_bytes(file).constData(),H5F_ACC_RDONLY,H5P_DEFAULT),H5Fclose};
  Handle dataset{fid.id>=0?H5Dopen2(fid.id,path.c_str(),H5P_DEFAULT):-1,H5Dclose};
  if(dataset.id<0){out.error="Cannot open dataset";out.error_code="ReadError";return out;}
  Handle space{H5Dget_space(dataset.id),H5Sclose};const int rank=H5Sget_simple_extent_ndims(space.id);
  if(rank<2){out.error="Variable requires at least two dimensions";out.error_code="InvalidSpec";return out;}
  out.reader="HDF5";out.shape.resize(rank);H5Sget_simple_extent_dims(space.id,out.shape.data(),nullptr);
  Handle type{H5Dget_type(dataset.id),H5Tclose};const auto category=H5Tget_class(type.id);const auto width=H5Tget_size(type.id);
  if((category!=H5T_INTEGER&&category!=H5T_FLOAT)||(category==H5T_FLOAT&&width!=4&&width!=8)||(category==H5T_INTEGER&&width!=1&&width!=2&&width!=4&&width!=8)){out.error="Unsupported HDF5 element type";out.error_code="UnsupportedType";return out;}
  out.storage_type=QString("%1%2").arg(category==H5T_FLOAT?"float":H5Tget_sign(type.id)==H5T_SGN_NONE?"uint":"int").arg(width*8);
  Handle properties{H5Dget_create_plist(dataset.id),H5Pclose};if(H5Pget_layout(properties.id)==H5D_CHUNKED){std::vector<hsize_t> chunks(rank);if(H5Pget_chunk(properties.id,rank,chunks.data())==rank){quint64 bytes=width;for(auto length:chunks){if(!length||bytes>std::numeric_limits<quint64>::max()/length){out.error="Chunk byte size overflow";out.error_code="MemoryBudgetExceeded";return out;}bytes*=length;}out.chunk_bytes=bytes;}}
  for(int axis=0;axis<rank;++axis){SliceAxisDescriptor descriptor;descriptor.dimension_index=axis;descriptor.length=out.shape[axis];descriptor.dimension_name=QString("axis %1").arg(axis);out.axes.push_back(descriptor);}
  return out;
}
std::vector<double> slice_read_coordinate(const QString &file,const SliceAxisDescriptor &axis,QString &error,size_t index){
  error.clear();if(axis.coordinate_path.isEmpty())return {};
  if(index==size_t(-1)&&axis.length>16ull*1024*1024){error="Coordinate too large";return {};}
  if(index!=size_t(-1)&&index>=axis.length){error="Coordinate index outside axis";return {};}
  NcReader reader(file);int group=-1,variable=-1,rank=0;
  if(!reader.locate(axis.coordinate_path.toStdString(),group,variable)||nc_inq_varndims(group,variable,&rank)!=NC_NOERR||rank!=1){error="Coordinate unavailable";return {};}
  int dim=-1;size_t length=0;nc_inq_vardimid(group,variable,&dim);nc_inq_dimlen(group,dim,&length);
  if(length!=axis.length){error="Coordinate shape changed";return {};}
  auto result=reader.read_coordinate(axis.coordinate_path.toStdString(),index);
  if(!result.error.isEmpty()){error=result.error;return {};}
  if(result.precision_warning){error="Coordinate exceeds exact float64 integer precision; using index labels";return {};}
  return std::move(result.plane.values);
}
QString slice_nearest(const std::vector<double> &values,double requested,size_t &index) {
  if(values.empty()||!std::isfinite(requested))return "No finite coordinate";
  const bool ascending=values.size()<2||values.back()>values.front();
  for(size_t i=0;i<values.size();++i)if(!std::isfinite(values[i])||(i&&!(ascending?values[i]>values[i-1]:values[i]<values[i-1])))return "Coordinate is not strictly monotonic and finite";
  if(requested<std::min(values.front(),values.back())||requested>std::max(values.front(),values.back()))return "Coordinate is outside the source range";
  index=0;long double distance=std::fabs((long double)values[0]-requested);
  for(size_t i=1;i<values.size();++i){const long double delta=std::fabs((long double)values[i]-requested);if(delta<distance){index=i;distance=delta;}}
  return {};
}

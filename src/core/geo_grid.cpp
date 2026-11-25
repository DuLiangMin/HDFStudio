// NetCDF entry points run only in the same-directory CLI worker.
#include "geo_grid.hpp"
#include "slice_coordinates.hpp"
#include "nc_reader.hpp"
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace GeoGrid {
namespace {
constexpr size_t max_axis=1048576;
QString text(int group,int variable,const char *name){size_t size=0;nc_type type;if(nc_inq_att(group,variable,name,&type,&size)!=NC_NOERR)return {};
  if(type==NC_CHAR&&size<=65536){QByteArray bytes(int(size),'\0');if(nc_get_att_text(group,variable,name,bytes.data())==NC_NOERR)return QString::fromUtf8(bytes).trimmed();}
  if(type==NC_STRING&&size==1){char *raw=nullptr;if(nc_get_att_string(group,variable,name,&raw)==NC_NOERR){const auto result=QString::fromUtf8(raw);nc_free_string(1,&raw);return result.trimmed();}}return {};}
bool number(int group,int variable,const char *name,double &value){size_t count=0;return nc_inq_attlen(group,variable,name,&count)==NC_NOERR&&count==1&&nc_get_att_double(group,variable,name,&value)==NC_NOERR&&std::isfinite(value);}
QString axis_check(Axis &axis,bool longitude){
  if(axis.centers.size()<2||axis.centers.size()>max_axis)return "Axis requires 2..1048576 centers";
  if(longitude){for(size_t i=1;i<axis.centers.size();++i){double &value=axis.centers[i];const double prior=axis.centers[i-1];if(std::abs(value-prior)>180&&std::abs(value-prior)<360){value+=360*std::round((prior-value)/360);}}}
  const bool ascending=axis.centers[1]>axis.centers[0];
  for(size_t i=0;i<axis.centers.size();++i){const double value=axis.centers[i];if(!std::isfinite(value)||std::abs(value)>(longitude?1e6:90)||(i&&!(ascending?value>axis.centers[i-1]:value<axis.centers[i-1])))return "Coordinates must be finite, in range and strictly monotonic";}
  const double span=std::abs(axis.centers.back()-axis.centers.front());if(longitude&&span>360+1e-7)return "Longitude axis spans more than 360 degrees";
  if(!axis.bounds.empty()){
    if(axis.bounds.size()!=axis.centers.size())return "Bounds length mismatch";
    for(size_t i=0;i<axis.bounds.size();++i){auto &bound=axis.bounds[i];if(longitude){bound.first+=360*std::round((axis.centers[i]-bound.first)/360);bound.second+=360*std::round((axis.centers[i]-bound.second)/360);}
      const double low=std::min(bound.first,bound.second),high=std::max(bound.first,bound.second);
      if(!std::isfinite(low)||!std::isfinite(high)||low>=high||axis.centers[i]<low-1e-8||axis.centers[i]>high+1e-8||(!longitude&&(low< -90||high>90))||(ascending?bound.first>=bound.second:bound.first<=bound.second))return "Invalid coordinate bounds";
      if(i){const auto &before=axis.bounds[i-1];if(ascending?std::max(before.first,before.second)>low+1e-8:std::min(before.first,before.second)<high-1e-8)return "Coordinate bounds overlap";}}
  }
  axis.periodic=false;
  if(longitude){const double step=std::abs(axis.centers[1]-axis.centers[0]);bool regular=true;for(size_t i=2;i<axis.centers.size();++i)if(std::abs(std::abs(axis.centers[i]-axis.centers[i-1])-step)>1e-7*std::max(1.0,step))regular=false;
    axis.periodic=regular&&(std::abs(span-360)<1e-7||std::abs(span+step-360)<1e-7);
    if(!axis.bounds.empty()){const auto first=axis.bounds.front(),last=axis.bounds.back();const double covered=std::max({first.first,first.second,last.first,last.second})-std::min({first.first,first.second,last.first,last.second});bool contiguous=true;
      for(size_t i=1;i<axis.bounds.size();++i)if(std::abs(axis.bounds[i-1].second-axis.bounds[i].first)>1e-7)contiguous=false;axis.periodic=contiguous&&std::abs(covered-360)<1e-7;}}
  return {};
}
QJsonArray array(const std::vector<double> &values){QJsonArray out;for(double value:values)out.append(value);return out;}
QJsonObject axis_json(const Axis &axis){QJsonArray bounds;for(const auto &b:axis.bounds)bounds.append(QJsonArray{b.first,b.second});return {{"centers",array(axis.centers)},{"bounds",bounds}};}
Axis parse_axis(const QJsonObject &object){Axis axis;for(const auto &v:object["centers"].toArray())axis.centers.push_back(v.isDouble()?v.toDouble():NAN);for(const auto &v:object["bounds"].toArray()){const auto pair=v.toArray();axis.bounds.push_back({pair.size()==2?pair[0].toDouble(NAN):NAN,pair.size()==2?pair[1].toDouble(NAN):NAN});}return axis;}
}
QString validate(Grid &grid){auto error=axis_check(grid.latitude,false);if(error.isEmpty())error=axis_check(grid.longitude,true);
  // ecCodes verifies the global Gaussian topology using the encoded grid definition.
  // Rounded GRIB endpoints need not sum to exactly 360 with their computed step.
  if(error.isEmpty()&&grid.source_definition.value("gridType").toString()=="regular_gg"&&grid.source_definition.value("global").toString()=="1")grid.longitude.periodic=true;
  grid.duplicate_endpoint=grid.longitude.periodic&&std::abs(std::abs(grid.longitude.centers.back()-grid.longitude.centers.front())-360)<1e-7;return grid.error=error;}
QString validate_frame(const Grid &g,const std::vector<double>&values,const std::vector<double>&original){const size_t rows=g.latitude.centers.size(),cols=g.longitude.centers.size();if(!rows||!cols||rows>SIZE_MAX/cols||values.size()!=rows*cols||original.size()!=values.size())return "Plane/coordinate shape mismatch";
  if(g.duplicate_endpoint)for(size_t r=0;r<rows;++r){const size_t a=r*cols,b=a+cols-1;auto equal=[](double x,double y){return (!std::isfinite(x)&&!std::isfinite(y))||x==y;};if(!equal(values[a],values[b])||!equal(original[a],original[b]))return "Conflicting repeated longitude endpoint values/Mask";}return {};}
qint64 nearest(const Axis &axis,double coordinate,bool longitude){if(axis.centers.empty()||!std::isfinite(coordinate))return -1;const bool ascending=axis.centers.back()>axis.centers.front();double low=std::min(axis.centers.front(),axis.centers.back()),high=std::max(axis.centers.front(),axis.centers.back());
  if(!axis.bounds.empty()){low=std::min(axis.bounds.front().first,axis.bounds.front().second);high=std::max(axis.bounds.back().first,axis.bounds.back().second);if(!ascending){low=std::min(axis.bounds.back().first,axis.bounds.back().second);high=std::max(axis.bounds.front().first,axis.bounds.front().second);}}
  if(longitude)coordinate=MapProjection::unwrap(coordinate,low-(axis.periodic&&axis.bounds.empty()?std::abs(axis.centers[1]-axis.centers[0])/2:0));
  if(!axis.periodic&&(coordinate<low-1e-8||coordinate>high+1e-8))return -1;
  if(!axis.bounds.empty()){
    // Bounds are ordered and non-overlapping after validation. Avoid an O(N)
    // bounds scan for every displayed pixel on large scientific grids.
    const auto interval=ascending
      ?std::lower_bound(axis.bounds.begin(),axis.bounds.end(),coordinate,[](const auto &b,double value){return std::max(b.first,b.second)<value-1e-8;})
      :std::lower_bound(axis.bounds.begin(),axis.bounds.end(),coordinate,[](const auto &b,double value){return std::min(b.first,b.second)>value+1e-8;});
    if(interval==axis.bounds.end()||coordinate<std::min(interval->first,interval->second)-1e-8||coordinate>std::max(interval->first,interval->second)+1e-8)return -1;
  }
  const auto it=ascending?std::lower_bound(axis.centers.begin(),axis.centers.end(),coordinate):std::lower_bound(axis.centers.begin(),axis.centers.end(),coordinate,std::greater<double>());
  size_t best=0;double distance=INFINITY;auto consider=[&](size_t i){double delta=std::abs(axis.centers[i]-coordinate);if(axis.periodic)delta=std::min(delta,std::abs(360-delta));if(delta<distance-1e-10||(std::abs(delta-distance)<=1e-10&&i<best)){distance=delta;best=i;}};
  if(it!=axis.centers.end())consider(size_t(it-axis.centers.begin()));if(it!=axis.centers.begin())consider(size_t(it-axis.centers.begin()-1));if(axis.periodic){consider(0);consider(axis.centers.size()-1);}return qint64(best);
}
Grid read_netcdf(const QString &file,const QString &variable,size_t rows,size_t cols){Grid g;const auto info=slice_inspect(file,variable.toStdString());
  if(!info.error.isEmpty()){g.error=info.error;return g;}if(!info.netcdf_identity||info.axes.size()<2){g.error="Verified NetCDF dimension identity required";return g;}
  const auto &lat=info.axes[info.axes.size()-2],&lon=info.axes.back();if(lat.role!=SliceAxisRole::Latitude||lon.role!=SliceAxisRole::Longitude||lat.length!=rows||lon.length!=cols){g.error="Last two dimensions must be verified latitude, longitude in plane order";return g;}
  if(rows>max_axis||cols>max_axis){g.error="Coordinate length exceeds limit";return g;}
  g.latitude.centers=slice_read_coordinate(file,lat,g.error);if(!g.error.isEmpty())return g;g.longitude.centers=slice_read_coordinate(file,lon,g.error);if(!g.error.isEmpty())return g;
  NcReader reader(file);int group=-1,var=-1;reader.locate(variable.toStdString(),group,var);
  auto read_bounds=[&](const SliceAxisDescriptor &descriptor,Axis &axis){int cg=-1,cv=-1;reader.locate(descriptor.coordinate_path.toStdString(),cg,cv);const auto bound_name=text(cg,cv,"bounds");if(bound_name.isEmpty())return;
    int bg=cg,bv=-1;if(bound_name.contains('/')){const auto parent=descriptor.coordinate_path.left(descriptor.coordinate_path.lastIndexOf('/')+1);reader.locate((bound_name.startsWith('/')?bound_name:parent+bound_name).toStdString(),bg,bv);}else nc_inq_varid(cg,bound_name.toUtf8().constData(),&bv);
    int rank=0;int dims[2]={};size_t n=0,two=0;int center_dim=-1;nc_inq_vardimid(cg,cv,&center_dim);
    if(bv<0||nc_inq_varndims(bg,bv,&rank)!=NC_NOERR||rank!=2||nc_inq_vardimid(bg,bv,dims)!=NC_NOERR||dims[0]!=center_dim||nc_inq_dimlen(bg,dims[0],&n)!=NC_NOERR||nc_inq_dimlen(bg,dims[1],&two)!=NC_NOERR||n!=axis.centers.size()||two!=2){g.error="Invalid coordinate bounds identity/shape";return;}
    size_t length=0;nc_inq_grpname_full(bg,&length,nullptr);QByteArray path(int(length)+1,'\0');nc_inq_grpname_full(bg,nullptr,path.data());char name[NC_MAX_NAME+1]={};nc_inq_varname(bg,bv,name);NcSliceSpec spec;spec.row_axis=0;spec.col_axis=1;spec.fixed_indices={0,0};auto decoded=reader.read_slice((QString::fromUtf8(path.constData())+"/"+QString::fromUtf8(name)).replace("//","/").toStdString(),spec);
    if(!decoded.error.isEmpty()||decoded.precision_warning||decoded.plane.values.size()!=n*2){g.error="Cannot decode coordinate bounds";return;}for(size_t i=0;i<n;++i)axis.bounds.push_back({decoded.plane.values[2*i],decoded.plane.values[2*i+1]});};
  read_bounds(lat,g.latitude);read_bounds(lon,g.longitude);if(!g.error.isEmpty())return g;
  const auto mapping=text(group,var,"grid_mapping");int mg=group,mv=-1;
  g.source_definition["grid_mapping"]=mapping;
  if(!mapping.isEmpty()) {if(mapping.contains(':')||mapping.contains(' ')){g.error="Multiple/extended grid mappings require a separate adapter";return g;}
    if(mapping.startsWith('/'))reader.locate(mapping.toStdString(),mg,mv);else{int scope=group;while(scope>=0){if(nc_inq_varid(scope,mapping.toUtf8().constData(),&mv)==NC_NOERR){mg=scope;break;}int parent=-1;if(nc_inq_grp_parent(scope,&parent)!=NC_NOERR)break;scope=parent;}}
    if(mv<0){g.error="Declared grid_mapping unavailable";return g;}if(text(mg,mv,"grid_mapping_name")!="latitude_longitude"){g.error="Unsupported source grid_mapping";return g;}
    double prime=0;if(number(mg,mv,"longitude_of_prime_meridian",prime)&&prime!=0){g.error="Non-Greenwich prime meridian requires verified normalization";return g;}
    double radius=NAN,a=NAN,b=NAN,inverse=NAN;number(mg,mv,"earth_radius",radius);number(mg,mv,"semi_major_axis",a);number(mg,mv,"semi_minor_axis",b);number(mg,mv,"inverse_flattening",inverse);
    for(const auto &attribute:{"earth_radius","semi_major_axis","semi_minor_axis","inverse_flattening","longitude_of_prime_meridian"}){nc_type type;size_t count=0;double ignored=0;if(nc_inq_att(mg,mv,attribute,&type,&count)==NC_NOERR&&!number(mg,mv,attribute,ignored)){g.error="Invalid declared numeric model attribute";return g;}}
    if(std::isfinite(a)&&std::isfinite(b)&&std::isfinite(inverse)){if(inverse<0||(inverse>0&&inverse<=1)||std::abs(b-(inverse==0?a:a*(1-1/inverse)))>1e-4){g.error="Conflicting flattening/semi-minor model";return g;}}
    if(std::isfinite(a)&&!std::isfinite(b)&&std::isfinite(inverse)&&inverse>=0)b=inverse==0?a:a*(1-1/inverse);
    if(std::isfinite(radius)){if((std::isfinite(a)&&a!=radius)||(std::isfinite(b)&&b!=radius)||(std::isfinite(inverse)&&inverse!=0)){g.error="Conflicting source earth models";return g;}a=b=radius;}
    if(std::isfinite(a)||std::isfinite(b)){g.earth={a,b,"CF grid_mapping: "+mapping};if(!g.earth.valid()){g.error="Invalid declared earth model";return g;}}
    g.source_definition["grid_mapping_name"]=text(mg,mv,"grid_mapping_name");
    for(const auto &name:{"earth_radius","semi_major_axis","semi_minor_axis","inverse_flattening","longitude_of_prime_meridian"}){double value;if(number(mg,mv,name,value))g.source_definition[name]=value;}
    const auto wkt=text(mg,mv,"crs_wkt"),spatial=text(mg,mv,"spatial_ref");g.source_definition["crs_wkt"]=wkt;g.source_definition["spatial_ref"]=spatial;
    for(const auto &definition:{wkt,spatial})if(!definition.isEmpty()){
      QString error;const auto model=MapProjection::Transform::model_from_wkt(definition,error);if(!error.isEmpty()){g.error=error;return g;}
      if(g.earth.valid()&&(std::abs(model.a-g.earth.a)>1e-4||std::abs(model.b-g.earth.b)>1e-4)){g.error="Conflicting WKT/CF earth models";return g;}g.earth=model;}}
  for(const auto &name:{"crs_wkt","spatial_ref"}){const auto definition=text(group,var,name);if(!definition.isEmpty()){g.source_definition[QString("variable_")+name]=definition;QString error;const auto model=MapProjection::Transform::model_from_wkt(definition,error);if(!error.isEmpty()){g.error=error;return g;}if(g.earth.valid()&&(std::abs(model.a-g.earth.a)>1e-4||std::abs(model.b-g.earth.b)>1e-4)){g.error="Conflicting variable WKT/model";return g;}g.earth=model;}}
  g.identity=stamp(file)+":"+variable+":"+lat.coordinate_path+":"+lon.coordinate_path;
  g.evidence+="CF decoded 1-D axes: "+lat.coordinate_path+" / "+lon.coordinate_path+"; Greenwich angular coordinates; datum not transformed";validate(g);return g;
}
Grid from_grib(const DataSource::PlaneData &plane,const QMap<QString,QString>&keys,const QString &kind){Grid g;
  for(auto it=keys.cbegin();it!=keys.cend();++it)g.source_definition[it.key()]=it.value();
  const size_t rows=plane.rows,cols=plane.cols;if((kind!="regular_ll"&&kind!="regular_gg")||!rows||!cols||rows>max_axis||cols>max_axis||rows>SIZE_MAX/cols||plane.coordinates.latitudes.size()!=rows*cols||plane.coordinates.longitudes.size()!=rows*cols){g.error="GRIB requires regular_ll or regular_gg and complete aligned coordinates";return g;}
  for(size_t r=0;r<rows;++r)g.latitude.centers.push_back(plane.coordinates.latitudes[r*cols]);for(size_t c=0;c<cols;++c)g.longitude.centers.push_back(plane.coordinates.longitudes[c]);
  for(size_t r=0;r<rows;++r)for(size_t c=0;c<cols;++c){const size_t i=r*cols+c;const double lat=plane.coordinates.latitudes[i],lon=plane.coordinates.longitudes[i];if(!std::isfinite(lat)||!std::isfinite(lon)||std::abs(lat-g.latitude.centers[r])>1e-7||std::abs(std::remainder(lon-g.longitude.centers[c],360))>1e-7){g.error="GRIB coordinate array is not separable/aligned";return g;}}
  bool radius_ok=false,a_ok=false,b_ok=false;const double radius=keys.value("radius").toDouble(&radius_ok),a=keys.value("earthMajorAxis").toDouble(&a_ok),b=keys.value("earthMinorAxis").toDouble(&b_ok);
  if(keys.value("earthIsOblate")=="0"&&radius_ok)g.earth={radius,radius,"ecCodes edition "+keys.value("edition")+" computed radius"};
  else if(keys.value("earthIsOblate")=="1"&&a_ok&&b_ok)g.earth={a,b,"ecCodes edition "+keys.value("edition")+" computed axes"};
  // Gaussian latitude centres are nonuniform: retain ecCodes' actual axis for nearest lookup.
  g.identity=plane.grid_identity;g.evidence="ecCodes aligned coordinates; "+kind+" separation checked; source scanning retained; datum not transformed";validate(g);return g;
}
QJsonObject json(const Grid &g){return {{"latitude",axis_json(g.latitude)},{"longitude",axis_json(g.longitude)},{"a",g.earth.a},{"b",g.earth.b},{"model_evidence",g.earth.evidence},{"source_definition",g.source_definition},{"identity",g.identity},{"evidence",g.evidence},{"error",g.error}};}
Grid from_json(const QJsonObject &o){Grid g;g.latitude=parse_axis(o["latitude"].toObject());g.longitude=parse_axis(o["longitude"].toObject());g.earth={o["a"].toDouble(),o["b"].toDouble(),o["model_evidence"].toString()};g.source_definition=o["source_definition"].toObject();g.identity=o["identity"].toString();g.evidence=o["evidence"].toString();g.error=o["error"].toString();if(g.error.isEmpty())validate(g);return g;}
QString stamp(const QString &file){QFileInfo info(file);return info.canonicalFilePath()+":"+QString::number(info.size())+":"+QString::number(info.lastModified().toMSecsSinceEpoch());}
int worker(const QString &request_file){QFile input(request_file);if(!input.open(QIODevice::ReadOnly)||input.size()>1024*1024)return 2;const auto request=QJsonDocument::fromJson(input.readAll()).object();
  Grid grid;const auto file=request["file"].toString();const auto shape=request["expected_shape"].toArray();const int rows=shape.size()==2?shape[0].toInt():0,cols=shape.size()==2?shape[1].toInt():0;
  if(request["schema"].toInt()!=1||request["request_id"].toString().isEmpty()||rows<2||cols<2||rows>int(max_axis)||cols>int(max_axis)||stamp(file)!=request["source_stamp"].toString())grid.error="Invalid geo-grid-v1 request or source changed";
  else grid=read_netcdf(file,request["variable"].toString(),size_t(rows),size_t(cols));if(stamp(file)!=request["source_stamp"].toString())grid.error="SourceChanged during coordinate read";
  QJsonObject out=request;out["grid"]=json(grid);const auto bytes=QJsonDocument(out).toJson(QJsonDocument::Compact);QSaveFile output(QFileInfo(request_file).absolutePath()+"/grid.json");if(bytes.size()>64*1024*1024||!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())return 4;return 0;
}
}

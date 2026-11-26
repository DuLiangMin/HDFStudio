// NetCDF export stays on the GUI/CLI thread; numerical workers never call netCDF-C.
#include "icosahedral.hpp"
#include "data_source.hpp"
#include "grib/grib_data.hpp"
#include "slice_io.hpp"
#include "nc_reader.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QTextStream>
#include <netcdf.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Icosahedral {
namespace {
void check(int code){if(code!=NC_NOERR)throw std::runtime_error(nc_strerror(code));}
void attribute(int file,int variable,const char *name,const QString &value){const auto bytes=value.toUtf8();check(nc_put_att_text(file,variable,name,size_t(bytes.size()),bytes.constData()));}
int dimension(int file,const char *name,size_t count){int id;check(nc_def_dim(file,name,count,&id));return id;}
int variable(int file,const char *name,nc_type type,const std::vector<int> &dims){int id;check(nc_def_var(file,name,type,int(dims.size()),dims.data(),&id));return id;}
}
QString export_netcdf(const QString &path,const Result &r){
  if(!r.error.isEmpty())return r.error;
  if(r.mesh.xyz.empty()||!r.source.values||r.restored.size()!=r.source.rows*r.source.cols)return "No completed analysis to export";
  if(r.mesh.atlas.triangles.size()!=r.mesh.faces.size()||r.mesh.atlas.node_chart.size()+2!=r.mesh.xyz.size()||r.node_values.size()!=r.mesh.xyz.size())return "Incomplete five-chart topology or node values";
  if(!r.source.file_path.isEmpty()&&slice_same_file(path,r.source.file_path))return "Cannot overwrite the source dataset";
  if(QFileInfo::exists(path))return "Output already exists; choose a new file";
  QTemporaryFile temporary(QFileInfo(path).absolutePath()+"/.hdfstudio-ico-XXXXXX.nc");
  if(!temporary.open())return temporary.errorString();temporary.close();
  int file=-1;
  try {
    check(nc_create(nc_path_bytes(temporary.fileName()).constData(),NC_CLOBBER|NC_64BIT_OFFSET,&file));
    const int node=dimension(file,"node",r.mesh.xyz.size()),face=dimension(file,"face",r.mesh.faces.size()),edge=dimension(file,"edge",r.mesh.edges.size());
    const int three=dimension(file,"three",3),two=dimension(file,"two",2),lat=dimension(file,"src_lat",r.source.rows),lon=dimension(file,"src_lon",r.source.cols);
    const int offset=dimension(file,"node_plus_one",r.forward_offset.size());
    // Classic NetCDF has no zero-length fixed dimension. Export padded storage, explicit valid_link_count.
    const int links=dimension(file,"forward_link",std::max(size_t(1),r.forward_source.size()));
    const int topology=variable(file,"mesh",NC_INT,{});
    attribute(file,topology,"cf_role","mesh_topology");int dim=2;check(nc_put_att_int(file,topology,"topology_dimension",NC_INT,1,&dim));
    attribute(file,topology,"node_coordinates","node_lon node_lat");attribute(file,topology,"face_node_connectivity","face_node");attribute(file,topology,"edge_node_connectivity","edge_node");
    const int xyz=variable(file,"node_xyz",NC_DOUBLE,{node,three}),fn=variable(file,"face_node",NC_INT,{face,three}),en=variable(file,"edge_node",NC_INT,{edge,two});
    const int chart=dimension(file,"chart",5),chart_y=dimension(file,"chart_y",size_t(1)<<r.mesh.level),chart_x=dimension(file,"chart_x",size_t(2)<<r.mesh.level);
    const int face_chart=variable(file,"face_chart",NC_INT,{face}),face_quad=variable(file,"face_chart_quad",NC_INT,{face}),chart_uv=variable(file,"face_chart_uv",NC_DOUBLE,{face,three,two});
    const int owner=variable(file,"chart_node",NC_INT,{chart,chart_y,chart_x}),chart_value=variable(file,"chart_value",NC_DOUBLE,{chart,chart_y,chart_x});
    const int apex_node=variable(file,"chart_apex_node",NC_INT,{two}),apex_value=variable(file,"chart_apex_value",NC_DOUBLE,{two});
    attribute(file,owner,"long_name","Half-open chart ownership; all nodes except chart apex nodes 5 and 6, each exactly once");
    attribute(file,apex_node,"long_name","Atlas endpoints, not geographical poles in the unchanged native mesh orientation");
    for(int id:{face_chart,face_quad,owner,apex_node}){int start=0;check(nc_put_att_int(file,id,"start_index",NC_INT,1,&start));}
    for(int id:{chart_value,apex_value}){const double nan=NAN;check(nc_put_att_double(file,id,"_FillValue",NC_DOUBLE,1,&nan));attribute(file,id,"units",r.source.units);}
    attribute(file,fn,"cf_role","face_node_connectivity");attribute(file,en,"cf_role","edge_node_connectivity");int zero=0;
    check(nc_put_att_int(file,fn,"start_index",NC_INT,1,&zero));check(nc_put_att_int(file,en,"start_index",NC_INT,1,&zero));
    const int nlon=variable(file,"node_lon",NC_DOUBLE,{node}),nlat=variable(file,"node_lat",NC_DOUBLE,{node}),area=variable(file,"face_area",NC_DOUBLE,{face});
    attribute(file,nlon,"units","degrees_east");attribute(file,nlat,"units","degrees_north");attribute(file,area,"units","sr");attribute(file,xyz,"units","1");
    const int slat=variable(file,"src_lat",NC_DOUBLE,{lat}),slon=variable(file,"src_lon",NC_DOUBLE,{lon});
    attribute(file,slat,"units","degrees_north");attribute(file,slon,"units","degrees_east");
    const int nv=variable(file,"node_value",NC_DOUBLE,{node}),ev=variable(file,"edge_value",NC_DOUBLE,{edge}),fv=variable(file,"face_vertex_mean",NC_DOUBLE,{face});
    const int sv=variable(file,"source_value",NC_DOUBLE,{lat,lon}),rv=variable(file,"restored",NC_DOUBLE,{lat,lon}),diff=variable(file,"residual",NC_DOUBLE,{lat,lon});
    const double missing=NAN;
    for(int id:{nv,ev,fv,sv,rv,diff}){attribute(file,id,"units",r.source.units);check(nc_put_att_double(file,id,"_FillValue",NC_DOUBLE,1,&missing));}
    attribute(file,nv,"mesh","mesh");attribute(file,nv,"location","node");attribute(file,ev,"mesh","mesh");attribute(file,ev,"location","edge");
    attribute(file,fv,"mesh","mesh");attribute(file,fv,"location","face");attribute(file,fv,"long_name","Arithmetic mean of three node point samples; NOT conservative cell mean");
    attribute(file,diff,"long_name","restored minus source; common finite source mask");
    const int row=variable(file,"forward_row_offset",NC_INT,{offset}),col=variable(file,"forward_source_index",NC_INT,{links}),weight=variable(file,"forward_weight",NC_DOUBLE,{links});
    check(nc_put_att_int(file,col,"start_index",NC_INT,1,&zero));attribute(file,col,"index_order","Original source row-major: row*source_cols+column");
    attribute(file,NC_GLOBAL,"title","HDFStudio 0.16.02 Icosahedral point analysis");
    attribute(file,NC_GLOBAL,"method",r.forward_method+"; radial barycentric reconstruction; point values, not area conservative");
    attribute(file,NC_GLOBAL,"geometry","Unit sphere, great-circle edges; face_area in steradians, not square metres; source datum not transformed");
    attribute(file,NC_GLOBAL,"source_variable",r.source.variable_path);attribute(file,NC_GLOBAL,"source_file_name",QFileInfo(r.source.file_path).fileName());
    attribute(file,NC_GLOBAL,"source_grid_evidence",r.grid.evidence);attribute(file,NC_GLOBAL,"source_time_identity",r.source.time_identity);
    QJsonArray indices;for(size_t i:r.source.leading_indices)indices.append(double(i));attribute(file,NC_GLOBAL,"source_leading_indices",QString::fromUtf8(QJsonDocument(indices).toJson(QJsonDocument::Compact)));
    const int level=r.mesh.level,valid_links=int(r.forward_source.size()),compared=int(r.compared);
    check(nc_put_att_int(file,NC_GLOBAL,"level",NC_INT,1,&level));check(nc_put_att_int(file,NC_GLOBAL,"valid_link_count",NC_INT,1,&valid_links));check(nc_put_att_int(file,NC_GLOBAL,"compared_count",NC_INT,1,&compared));
    for(const auto &entry:std::vector<std::pair<const char*,double>>{{"bias",r.bias},{"mae",r.mae},{"rmse",r.rmse}})check(nc_put_att_double(file,NC_GLOBAL,entry.first,NC_DOUBLE,1,&entry.second));
    check(nc_enddef(file));check(nc_put_var_int(file,topology,&zero));
    check(nc_put_var_double(file,xyz,r.mesh.xyz.front().data()));check(nc_put_var_int(file,fn,r.mesh.faces.front().data()));check(nc_put_var_int(file,en,r.mesh.edges.front().data()));
    std::vector<int> charts,quads;std::vector<double> uv,cards;
    for(const auto &tri:r.mesh.atlas.triangles){charts.push_back(tri.chart);quads.push_back(tri.quad);for(const auto &p:tri.uv){uv.push_back(p.u);uv.push_back(p.v);}}
    for(auto index:r.mesh.atlas.node_chart)cards.push_back(r.node_values[size_t(index)]);
    check(nc_put_var_int(file,face_chart,charts.data()));check(nc_put_var_int(file,face_quad,quads.data()));check(nc_put_var_double(file,chart_uv,uv.data()));
    check(nc_put_var_int(file,owner,r.mesh.atlas.node_chart.data()));check(nc_put_var_double(file,chart_value,cards.data()));check(nc_put_var_int(file,apex_node,r.mesh.atlas.apex.data()));
    const double apex_values[2]={r.node_values[r.mesh.atlas.apex[0]],r.node_values[r.mesh.atlas.apex[1]]};check(nc_put_var_double(file,apex_value,apex_values));
    std::vector<double> longitude,latitude;for(const auto &v:r.mesh.xyz){longitude.push_back(std::atan2(v[1],v[0])*180/3.14159265358979323846);latitude.push_back(std::asin(std::clamp(v[2],-1.,1.))*180/3.14159265358979323846);}
    check(nc_put_var_double(file,nlon,longitude.data()));check(nc_put_var_double(file,nlat,latitude.data()));check(nc_put_var_double(file,area,r.mesh.area.data()));
    check(nc_put_var_double(file,slat,r.grid.latitude.centers.data()));check(nc_put_var_double(file,slon,r.grid.longitude.centers.data()));
    check(nc_put_var_double(file,nv,r.node_values.data()));check(nc_put_var_double(file,ev,r.edge_values.data()));check(nc_put_var_double(file,fv,r.face_values.data()));
    check(nc_put_var_double(file,sv,r.source.values->data()));check(nc_put_var_double(file,rv,r.restored.data()));check(nc_put_var_double(file,diff,r.residual.data()));
    std::vector<int> offsets(r.forward_offset.begin(),r.forward_offset.end());check(nc_put_var_int(file,row,offsets.data()));
    if(valid_links){check(nc_put_var_int(file,col,r.forward_source.data()));check(nc_put_var_double(file,weight,r.forward_weight.data()));}
    else {check(nc_put_var_int(file,col,&zero));check(nc_put_var_double(file,weight,&missing));}
    const int closing=file;file=-1;check(nc_close(closing));
    if(!temporary.rename(path))return "Cannot finalize new NetCDF file: "+temporary.errorString();
    temporary.setAutoRemove(false);
  } catch(const std::exception &error){if(file>=0)nc_close(file);return QString::fromUtf8(error.what());}
  return {};
}
int command(const QStringList &args){
  const bool is_demo=args.value(1)=="ico-demo";const int level_position=is_demo?3:5;
  if((is_demo&&(args.size()<3||args.size()>4))||(!is_demo&&args.size()<5))return 2;
  bool ok=true;const int level=args.size()>level_position?args[level_position].toInt(&ok):3;
  if(!ok||level<0||level>6)return 2;
  PlaneSnapshot source;GeoGrid::Grid grid;const QString output=is_demo?args[2]:args[4];QString initial_stamp;
  if(is_demo)source=demo(grid);
  else {
    source.file_path=args[2];source.variable_path=args[3];initial_stamp=GeoGrid::stamp(source.file_path);
    if(slice_same_file(output,source.file_path)){QTextStream(stderr)<<"Cannot overwrite source\n";return 4;}
    const auto info=DataSource::describe(source.file_path,source.variable_path);
    if(!info.error.isEmpty()||info.shape.size()<2||info.shape.back()==0||info.shape[info.shape.size()-2]>4194304/info.shape.back()){QTextStream(stderr)<<"Unsupported or oversized plane\n";return 3;}
    for(int i=level_position+1;i<args.size();++i){const auto index=args[i].toULongLong(&ok);if(!ok||args[i].startsWith('-'))return 2;source.leading_indices.push_back(size_t(index));}
    if(source.leading_indices.empty())source.leading_indices.resize(info.shape.size()-2,0);
    auto plane=DataSource::grib_target(source.variable_path)?GribData::read(source.file_path,source.variable_path,source.leading_indices,true):DataSource::read_plane(source.file_path,source.variable_path,source.leading_indices);
    if(!plane.error.isEmpty()){QTextStream(stderr)<<plane.error<<'\n';return 3;}
    source.rows=plane.rows;source.cols=plane.cols;source.units=plane.units.isEmpty()?QString("unknown"):plane.units;source.time_identity=plane.time_identity;source.grid_identity=plane.grid_identity;
    if(!DataSource::grib_target(source.variable_path)){NcReader reader(source.file_path);int group=-1,var=-1;size_t length=0;if(reader.locate(source.variable_path.toStdString(),group,var)&&nc_inq_attlen(group,var,"units",&length)==NC_NOERR&&length<=65536){QByteArray bytes(int(length),'\0');if(nc_get_att_text(group,var,"units",bytes.data())==NC_NOERR)source.units=QString::fromUtf8(bytes);}}
    grid=DataSource::grib_target(source.variable_path)?GeoGrid::from_grib(plane,plane.geographic_keys,info.grid_kind):GeoGrid::read_netcdf(source.file_path,source.variable_path,plane.rows,plane.cols);
    source.values=std::make_shared<const std::vector<double>>(std::move(plane.values));
    if(GeoGrid::stamp(source.file_path)!=initial_stamp){QTextStream(stderr)<<"SourceChanged\n";return 3;}
  }
  const auto result=analyze(source,grid,level);
  if(!result->error.isEmpty()){QTextStream(stderr)<<result->error<<'\n';return 3;}
  if(!is_demo&&GeoGrid::stamp(source.file_path)!=initial_stamp){QTextStream(stderr)<<"SourceChanged\n";return 3;}
  const auto error=export_netcdf(output,*result);if(!error.isEmpty()){QTextStream(stderr)<<error<<'\n';return 4;}
  QJsonObject report{{"version","0.16.02"},{"level",level},{"nodes",double(result->mesh.xyz.size())},{"faces",double(result->mesh.faces.size())},{"edges",double(result->mesh.edges.size())},{"compared",double(result->compared)},{"rmse",result->rmse},{"output",QFileInfo(output).absoluteFilePath()}};
  QTextStream(stdout)<<QJsonDocument(report).toJson();return 0;
}
}

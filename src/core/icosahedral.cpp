// Adapt existing C17 algorithms; original source arrays and masks remain immutable.
#include "icosahedral.hpp"
extern "C" {
#include "26NOV21_scrip.h"
}
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace Icosahedral {
namespace {
constexpr double rad=SC_PI/180;
bool stopped(const Cancel &cancel){return cancel&&cancel();}
sc_vec native(const Vec &v){return {v[0],v[1],v[2]};}
Vec vector(sc_vec v){return {v.x,v.y,v.z};}
double dot(const Vec &a,const Vec &b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec unit(double lon,double lat){return vector(sc_lonlat(lon*rad,lat*rad));}
QString axes(const PlaneSnapshot &s,GeoGrid::Grid &g,std::vector<size_t> &rows,std::vector<size_t> &cols){
  if(!g.error.isEmpty())return g.error;
  auto error=GeoGrid::validate(g);if(!error.isEmpty())return error;
  if(!s.values||s.out_of_range||s.rows<2||s.cols<4||s.rows>4194304/s.cols||s.values->size()!=s.rows*s.cols||g.latitude.centers.size()!=s.rows||g.longitude.centers.size()!=s.cols)
    return "A ready latitude/longitude plane with 2..4194304 cells and >=4 columns is required";
  error=GeoGrid::validate_frame(g,*s.values,*s.values);if(!error.isEmpty())return error;
  rows.resize(s.rows);std::iota(rows.begin(),rows.end(),0);
  if(g.latitude.centers.front()>g.latitude.centers.back())std::reverse(rows.begin(),rows.end());
  cols.resize(s.cols-(g.duplicate_endpoint?1:0));
  // Omit the last repeated endpoint in original order, then order increasing longitude.
  std::iota(cols.begin(),cols.end(),0);
  if(g.longitude.centers.front()>g.longitude.centers.back())std::reverse(cols.begin(),cols.end());
  if(cols.size()<4)return "At least four unique longitudes are required";
  if(g.longitude.centers[cols.back()]-g.longitude.centers[cols.front()]>=360)
    return "Unique longitude centers must span less than one period";
  return {};
}
}
quint64 estimate_bytes(size_t pixels,int level){
  if(level<0||level>6||pixels>4194304)return 0;
  return quint64(pixels)*96+(20ull<<(2*level))*600+96ull*1024*1024;
}
QString build_mesh(int level,Mesh &out,const Cancel &cancel){
  if(level<0||level>6)return "Interactive mesh level must be in 0..6";
  Mesh mesh;mesh.level=level;
  for(int depth=0;depth<=level;++depth){
    if(stopped(cancel))return "Cancelled";
    sc_mesh current={};const int code=sc_icosphere(depth,&current);
    if(code)return QString::fromLatin1(sc_error(code));
    auto &triangles=mesh.hierarchy.emplace_back();triangles.reserve(current.face_count);
    for(size_t f=0;f<current.face_count;++f){
      const auto a=current.xyz[current.faces[3*f]],b=current.xyz[current.faces[3*f+1]],c=current.xyz[current.faces[3*f+2]];
      const double det=sc_dot(a,sc_cross(b,c));
      if(det<=0){sc_mesh_free(&current);return "Invalid outward triangle orientation";}
      triangles.push_back({vector(sc_scale(sc_cross(b,c),1/det)),vector(sc_scale(sc_cross(c,a),1/det)),vector(sc_scale(sc_cross(a,b),1/det))});
      if(depth==level){mesh.faces.push_back({current.faces[3*f],current.faces[3*f+1],current.faces[3*f+2]});mesh.area.push_back(sc_triangle_area(a,b,c));}
    }
    if(depth==level)for(size_t i=0;i<current.vertex_count;++i)mesh.xyz.push_back(vector(current.xyz[i]));
    sc_mesh_free(&current);
  }
  std::map<Edge,int> edges;
  for(const auto &face:mesh.faces)for(int i=0;i<3;++i){int32_t a=face[i],b=face[(i+1)%3];if(a>b)std::swap(a,b);++edges[{a,b}];}
  for(const auto &entry:edges){if(entry.second!=2)return "Non-manifold mesh edge";mesh.edges.push_back(entry.first);}
  const auto atlas_error=build_atlas(mesh,mesh.atlas);if(!atlas_error.isEmpty())return atlas_error;
  out=std::move(mesh);return {};
}
QString locate(const Mesh &mesh,const Vec &point,size_t &face,Vec &weights){
  if(mesh.hierarchy.size()!=size_t(mesh.level+1)||mesh.hierarchy.empty())return "Missing mesh hierarchy";
  size_t chosen=0;
  for(int depth=0;depth<=mesh.level;++depth){
    const auto &triangles=mesh.hierarchy[size_t(depth)];const size_t begin=depth?chosen*4:0,end=depth?begin+4:triangles.size();
    bool found=false;
    for(size_t candidate=begin;candidate<end;++candidate){
      const auto &tri=triangles[candidate];Vec bary={dot(tri[0],point),dot(tri[1],point),dot(tri[2],point)};
      if(*std::min_element(bary.begin(),bary.end())< -2e-11)continue;
      double sum=0;for(auto &v:bary){v=std::max(0.,v);sum+=v;}
      if(!std::isfinite(sum)||sum<=0)return "Degenerate barycentric weights";
      for(auto &v:bary)v/=sum;weights=bary;chosen=candidate;found=true;break;
    }
    if(!found)return "Spherical face containment failed";
  }
  face=chosen;return {};
}
std::shared_ptr<Result> analyze(const PlaneSnapshot &source,const GeoGrid::Grid &coordinates,int level,const Cancel &cancel){
  auto out=std::make_shared<Result>();out->source=source;out->grid=coordinates;
  std::vector<size_t> rows,cols;out->error=axes(source,out->grid,rows,cols);if(!out->error.isEmpty())return out;
  out->error=build_mesh(level,out->mesh,cancel);if(!out->error.isEmpty())return out;
  if(stopped(cancel)){out->error="Cancelled";return out;}
  const size_t count=rows.size()*cols.size();
  std::vector<double> lat(count),lon(count),values(count);
  std::vector<int32_t> mask(count),target_mask(out->mesh.xyz.size(),1);
  double origin=out->grid.longitude.centers[cols.front()];origin-=360*std::floor((origin+180)/360);
  for(size_t r=0;r<rows.size();++r)for(size_t c=0;c<cols.size();++c){
    const size_t i=r*cols.size()+c;lat[i]=coordinates.latitude.centers[rows[r]]*rad;
    lon[i]=(origin+out->grid.longitude.centers[cols[c]]-out->grid.longitude.centers[cols.front()])*rad;values[i]=(*source.values)[rows[r]*source.cols+cols[c]];mask[i]=std::isfinite(values[i])?1:0;
  }
  std::vector<double> target_lat,target_lon;
  for(const auto &v:out->mesh.xyz){target_lat.push_back(std::asin(std::clamp(v[2],-1.,1.)));target_lon.push_back(std::atan2(v[1],v[0]));}
  sc_grid grid={};grid.count=count;grid.rank=2;grid.dims[0]=int(cols.size());grid.dims[1]=int(rows.size());grid.lat=lat.data();grid.lon=lon.data();grid.mask=mask.data();
  sc_csr csr={};sc_stats stats={};
  const bool periodic=out->grid.longitude.periodic;
  bool uniform=periodic;for(size_t c=0;c<cols.size();++c)if(std::abs(lon[c]-(lon[0]+2*SC_PI*c/cols.size()))>1e-14)uniform=false;
  if(uniform){
    const int code=sc_bilinear_build(&grid,target_lat.size(),target_lat.data(),target_lon.data(),target_mask.data(),&csr,&stats);
    if(code){out->error=QString::fromLatin1(sc_error(code));return out;}
    out->forward_method="06 C17 sc_bilinear_build (verified uniform longitude)";
  }else out->forward_method=periodic?"Existing Python bilinear formula adapted to actual longitude centers (no snapping)":"Regional lon-lat embedded on global icosphere; outside source centers = NaN; no longitude wrapping across regional boundary";
  out->node_values.resize(target_lat.size(),NAN);
  for(size_t node=0;node<target_lat.size();++node){
    out->forward_offset.push_back(out->forward_source.size());
    std::array<size_t,4> addresses;std::array<double,4> weights;
    if(uniform){
      if(csr.offset[node]==csr.offset[node+1])continue;
      for(size_t corner=0;corner<4;++corner){addresses[corner]=size_t(csr.source[csr.offset[node]+corner]);weights[corner]=csr.weight[csr.offset[node]+corner];}
    }else{
      const double latitude=target_lat[node];if(latitude<lat.front()||latitude>lat[(rows.size()-1)*cols.size()])continue;
      const auto row_it=std::upper_bound(rows.begin(),rows.end(),latitude,[&](double value,size_t row){return value<coordinates.latitude.centers[row]*rad;});
      const size_t r=std::min(rows.size()-2,size_t(row_it-rows.begin())-1);
      double longitude=std::fmod(target_lon[node]-lon[0],2*SC_PI);if(longitude<0)longitude+=2*SC_PI;longitude+=lon[0];
      if(!periodic&&longitude>lon[cols.size()-1])continue;
      const auto col_it=std::upper_bound(lon.begin(),lon.begin()+cols.size(),longitude);
      const size_t c=std::min(cols.size()-(periodic?1:2),size_t(col_it-lon.begin())-1),next=(c+1)%cols.size();
      const double east=c+1<cols.size()?lon[c+1]:lon[0]+2*SC_PI;
      const double x=(longitude-lon[c])/(east-lon[c]),y=(latitude-lat[r*cols.size()])/(lat[(r+1)*cols.size()]-lat[r*cols.size()]);
      addresses={r*cols.size()+c,r*cols.size()+next,(r+1)*cols.size()+next,(r+1)*cols.size()+c};weights={(1-x)*(1-y),x*(1-y),x*y,(1-x)*y};
      bool valid=true;for(size_t address:addresses)if(!mask[address])valid=false;if(!valid)continue;
    }
    double value=0;
    for(size_t corner=0;corner<4;++corner){
      const auto index=addresses[corner];value+=weights[corner]*values[index];
      out->forward_source.push_back(int32_t(rows[index/cols.size()]*source.cols+cols[index%cols.size()]));out->forward_weight.push_back(weights[corner]);
    }
    out->node_values[node]=value;
  }
  out->forward_offset.push_back(out->forward_source.size());sc_csr_free(&csr);
  for(const auto &e:out->mesh.edges)out->edge_values.push_back(out->node_values[e[0]]*.5+out->node_values[e[1]]*.5);
  for(const auto &f:out->mesh.faces)out->face_values.push_back(out->node_values[f[0]]/3+out->node_values[f[1]]/3+out->node_values[f[2]]/3);
  out->restored.resize(source.values->size(),NAN);out->residual.resize(source.values->size(),NAN);
  // Stable scaled error moments preserve small physical signals without squaring underflow.
  double scale=0,signed_sum=0,absolute_sum=0,square_sum=0;
  for(size_t r=0;r<source.rows;++r){
    if(stopped(cancel)){out->error="Cancelled";return out;}
    for(size_t c=0;c<source.cols;++c){
      const size_t i=r*source.cols+c;size_t face=0;Vec bary;
      out->error=locate(out->mesh,unit(coordinates.longitude.centers[c],coordinates.latitude.centers[r]),face,bary);if(!out->error.isEmpty())return out;
      const auto &nodes=out->mesh.faces[face];double value=0;bool valid=true;
      for(int k=0;k<3;++k){const double v=out->node_values[nodes[k]];if(!std::isfinite(v)){valid=false;break;}value+=bary[k]*v;}
      // Preserve the original source missing mask on reconstruction.
      if(!valid||!std::isfinite((*source.values)[i])||!std::isfinite(value))continue;
      out->restored[i]=value;const double error=value-(*source.values)[i];if(!std::isfinite(error))continue;
      out->residual[i]=error;++out->compared;const double magnitude=std::abs(error);
      if(magnitude>scale){const double factor=scale/magnitude;signed_sum*=factor;absolute_sum*=factor;square_sum*=factor*factor;scale=magnitude;}
      if(scale>0){const double scaled=error/scale;signed_sum+=scaled;absolute_sum+=std::abs(scaled);square_sum+=scaled*scaled;}
    }
  }
  if(out->compared){out->bias=scale*(signed_sum/out->compared);out->mae=scale*(absolute_sum/out->compared);out->rmse=scale*std::sqrt(square_sum/out->compared);}
  return out;
}
PlaneSnapshot demo(GeoGrid::Grid &grid){
  PlaneSnapshot source;source.rows=73;source.cols=144;source.units="dimensionless";source.variable_path="Analytic demo: sin(lat) + 0.5 cos(2 lon) cos(lat)";source.revision=1;
  auto values=std::make_shared<std::vector<double>>();
  for(size_t r=0;r<source.rows;++r)grid.latitude.centers.push_back(-90+2.5*r);
  for(size_t c=0;c<source.cols;++c)grid.longitude.centers.push_back(2.5*c);
  for(double lat:grid.latitude.centers)for(double lon:grid.longitude.centers)values->push_back(std::sin(lat*rad)+.5*std::cos(2*lon*rad)*std::cos(lat*rad));
  source.values=values;grid.evidence="Analytic demonstration; unit sphere, not a forecast";grid.identity="icosahedral-demo-v1";GeoGrid::validate(grid);source.grid_identity=grid.identity;return source;
}
}

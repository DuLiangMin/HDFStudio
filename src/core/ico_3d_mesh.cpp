// Copied/adapted from the compiled source project's ico_3d_mesh.cpp / ico_atlas.cpp.
// Changes: existing immutable Result instead of IcoAtlas; point/area modes, same five-chart indexing.
#include "ico_3d_mesh.hpp"
#include <QColor>
extern "C" {
#include "26NOV21_scrip.h"
}
#include <map>
#include <algorithm>
#include <cmath>
namespace {
sc_vec native(const Icosahedral::Vec &v){return {v[0],v[1],v[2]};}
sc_vec sc_sub(sc_vec a,sc_vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Ico3DVertex vertex(const Icosahedral::Mesh &mesh,const Ico3DBuffers &buffers,int id,QRgb color) {
  const auto &p=buffers.poly_xyz[size_t(id)],&s=mesh.xyz[size_t(id)];
  return {{float(p[0]),float(p[1]),float(p[2])},{float(s[0]),float(s[1]),float(s[2])},
          {qRed(color)/255.f,qGreen(color)/255.f,qBlue(color)/255.f}};
}
sc_vec position(const Icosahedral::Mesh &mesh,const Ico3DBuffers &buffers,int id,double alpha) {
  const auto &p=buffers.poly_xyz[size_t(id)],&s=mesh.xyz[size_t(id)];
  return {p[0]+(s[0]-p[0])*alpha,p[1]+(s[1]-p[1])*alpha,p[2]+(s[2]-p[2])*alpha};
}
bool box_hit(const std::array<double,6> &box,const sc_vec &origin,const sc_vec &direction) {
  const double o[3]={origin.x,origin.y,origin.z},d[3]={direction.x,direction.y,direction.z};
  double near=0,far=1e30;
  for(int k=0;k<3;++k) {
    if(std::abs(d[k])<1e-15) { if(o[k]<box[k]-1e-9||o[k]>box[k+3]+1e-9) return false; }
    else { double a=(box[k]-1e-9-o[k])/d[k],b=(box[k+3]+1e-9-o[k])/d[k];
      if(a>b) std::swap(a,b); near=std::max(near,a); far=std::min(far,b); if(near>far) return false; }
  }
  return true;
}
}
bool ico_build_3d_topology_buffers(const Icosahedral::Mesh &mesh,Ico3DBuffers &out) {
  out={};if(mesh.level<0||mesh.level>6||mesh.xyz.size()<12)return false;
  Icosahedral::Mesh base;if(!Icosahedral::build_mesh(0,base).isEmpty())return false;
  auto project=[&](sc_vec s) {
    double scale=1.;
    for(const auto &face:base.faces){
      const auto a=native(base.xyz[face[0]]),b=native(base.xyz[face[1]]),c=native(base.xyz[face[2]]);
      const auto normal=sc_cross(sc_sub(b,a),sc_sub(c,a));const double denominator=sc_dot(normal,s);
      if(denominator>1e-14)scale=std::min(scale,sc_dot(normal,a)/denominator);
    }
    return Icosahedral::Vec{scale*s.x,scale*s.y,scale*s.z};
  };
  for(const auto &v:mesh.xyz)out.poly_xyz.push_back(project(native(v)));
  const size_t per_base=size_t(1)<<(2*mesh.level);
  if(mesh.faces.size()!=20*per_base)return false;
  for(size_t group=0;group<20;++group){
    auto &box=out.bounds_3d[group];box={1,1,1,-1,-1,-1};
    for(size_t f=group*per_base;f<(group+1)*per_base;++f)for(int node:mesh.faces[f]){
      if(node<0||size_t(node)>=mesh.xyz.size())return false;
      for(const auto &v:{mesh.xyz[node],out.poly_xyz[node]})for(int k=0;k<3;++k){box[k]=std::min(box[k],v[k]);box[k+3]=std::max(box[k+3],v[k]);}
    }
  }
  struct Edge { size_t face; int count=0; };
  std::map<std::pair<int,int>,Edge> edges;
  out.fill_vertices.reserve(3*mesh.faces.size());
  for(size_t f=0;f<mesh.faces.size();++f) for(int k=0;k<3;++k) {
    int a=mesh.faces[f][k],b=mesh.faces[f][(k+1)%3];
    out.fill_vertices.push_back(vertex(mesh,out,a,qRgb(110,110,110)));
    if(a>b) std::swap(a,b);
    const auto found=edges.find({a,b});
    if(found==edges.end()) edges[{a,b}]={f,1};
    else {
      auto add=[&](std::vector<Ico3DVertex> &target,QRgb color) {
        target.push_back(vertex(mesh,out,a,color)); target.push_back(vertex(mesh,out,b,color));
      };
      ++found->second.count;
      add(out.sub_edge_lines,qRgb(55,63,73));
      if(found->second.face/per_base!=f/per_base) add(out.base_edge_lines,qRgb(18,27,40));
      if(mesh.atlas.triangles.size()==mesh.faces.size()&&mesh.atlas.triangles[found->second.face].chart!=mesh.atlas.triangles[f].chart)
        add(out.chart_seam_lines,QColor::fromHsv(mesh.atlas.triangles[f].chart*72,210,210).rgb());
    }
  }
  for(int ring=0;ring<3;++ring) for(int segment=0;segment<360;++segment) for(int end=0;end<2;++end) {
    const double a=(segment+end)*SC_PI/180.;
    const sc_vec s=ring==0?sc_vec{std::cos(a),std::sin(a),0}:
      (ring==1?sc_vec{std::cos(a),0,std::sin(a)}:sc_vec{0,std::cos(a),std::sin(a)});
    const auto p=project(s);
    out.graticule_lines.push_back({{float(p[0]),float(p[1]),float(p[2])},
      {float(s.x),float(s.y),float(s.z)},{.55f,.59f,.63f}});
  }
  return out.sub_edge_lines.size()==3*mesh.faces.size();
}
bool ico_update_3d_fill_colors(const Icosahedral::Result &result,bool area,const HdfColorMap::Palette &palette,
                             const HdfColorMap::Mapping &mapping,Ico3DBuffers &buffers) {
  if(!result.error.isEmpty()||!HdfColorMap::validate(palette,mapping).isEmpty()) return false;
  const auto &mesh=result.mesh;const auto &values=area?mesh.area:result.node_values;
  if(values.size()!=(area?mesh.faces.size():mesh.xyz.size())||buffers.fill_vertices.size()!=3*mesh.faces.size()) return false;
  for(size_t f=0;f<mesh.faces.size();++f) {
    bool missing=false;
    for(int k=0;k<3;++k) missing|=!std::isfinite(values[area?f:mesh.faces[f][k]]);
    for(int k=0;k<3;++k) {
      const QRgb color=missing?HdfColorMap::missing_color:HdfColorMap::map_value(palette,mapping,values[area?f:mesh.faces[f][k]]);
      auto &v=buffers.fill_vertices[3*f+k]; v.color[0]=qRed(color)/255.f; v.color[1]=qGreen(color)/255.f; v.color[2]=qBlue(color)/255.f;
    }
  }
  return true;
}
Ico3DPickHit ico_pick_3d_ray(const QVector3D &origin,const QVector3D &direction,double morph,
                           const Icosahedral::Result &result,const Ico3DBuffers &buffers,bool area) {
  Ico3DPickHit hit;
  if(!result.error.isEmpty()||!std::isfinite(morph)||direction.lengthSquared()<1e-20f) return hit;
  for(int axis=0;axis<3;++axis) if(!std::isfinite(origin[axis])||!std::isfinite(direction[axis])) return hit;
  const auto &mesh=result.mesh;const auto &values=area?mesh.area:result.node_values;
  if(buffers.poly_xyz.size()!=mesh.xyz.size()||values.size()!=(area?mesh.faces.size():mesh.xyz.size())) return hit;
  morph=std::clamp(morph,0.,1.);
  const sc_vec o{origin.x(),origin.y(),origin.z()},d{direction.x(),direction.y(),direction.z()};
  double nearest=1e30; const size_t per_base=size_t(1)<<(2*mesh.level);
  for(size_t base=0;base<20;++base) {
    if(!box_hit(buffers.bounds_3d[base],o,d)) continue;
    for(size_t f=base*per_base;f<(base+1)*per_base;++f) {
      const auto a=position(mesh,buffers,mesh.faces[f][0],morph),b=position(mesh,buffers,mesh.faces[f][1],morph),c=position(mesh,buffers,mesh.faces[f][2],morph);
      const auto ab=sc_sub(b,a),ac=sc_sub(c,a),p=sc_cross(d,ac); const double determinant=sc_dot(ab,p);
      if(determinant<1e-14) continue;
      const auto offset=sc_sub(o,a); const double u=sc_dot(offset,p)/determinant;
      const auto q=sc_cross(offset,ab); const double v=sc_dot(d,q)/determinant,t=sc_dot(ac,q)/determinant;
      if(u< -1e-9||v< -1e-9||u+v>1+1e-9||t<0||t>=nearest) continue;
      nearest=t; hit.hit=true; hit.face=int(f);
      const sc_vec intersection{o.x+t*d.x,o.y+t*d.y,o.z+t*d.z};
      const sc_vec corners[3]={a,b,c}; int corner=0; double distance=1e30;
      for(int k=0;k<3;++k) { const auto delta=sc_sub(corners[k],intersection); const double squared=sc_dot(delta,delta); if(squared<distance) { distance=squared; corner=k; } }
      hit.nearest_node=mesh.faces[f][corner];
      const auto point=sc_unit(intersection);
      hit.lon_deg=std::atan2(point.y,point.x)*180/SC_PI; hit.lat_deg=std::asin(std::clamp(point.z,-1.,1.))*180/SC_PI;
      hit.value=values[area?f:hit.nearest_node];
    }
  }
  return hit;
}

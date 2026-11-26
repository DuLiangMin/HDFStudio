// Copied/adapted from HDFStudio_to_Isosahedral ico_3d_mesh.* (2026-11-22).
// Input: immutable existing mesh/result; output: packed GL vertices and double ray picks.
#pragma once
#include "icosahedral.hpp"
#include "color_map.hpp"
#include <QVector3D>
#include <limits>
struct Ico3DVertex { float poly[3],sphere[3],color[3]; };
struct Ico3DBuffers {
  std::vector<Ico3DVertex> fill_vertices,sub_edge_lines,base_edge_lines,chart_seam_lines,graticule_lines;
  std::vector<Icosahedral::Vec> poly_xyz;
  std::array<std::array<double,6>,20> bounds_3d{};
};
struct Ico3DPickHit {
  bool hit=false;
  int face=-1,nearest_node=-1;
  double lon_deg=0,lat_deg=0,value=std::numeric_limits<double>::quiet_NaN();
};
bool ico_build_3d_topology_buffers(const Icosahedral::Mesh &mesh,Ico3DBuffers &out);
bool ico_update_3d_fill_colors(const Icosahedral::Result &result,bool area,
                             const HdfColorMap::Palette &palette,const HdfColorMap::Mapping &mapping,Ico3DBuffers &buffers);
Ico3DPickHit ico_pick_3d_ray(const QVector3D &origin,const QVector3D &direction,double morph,
                           const Icosahedral::Result &result,const Ico3DBuffers &buffers,bool area);

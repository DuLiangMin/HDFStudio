// Five-strip / rectangular-chart CPU rendering adapted from the existing ico_render.*.
#pragma once
#include "icosahedral.hpp"
#include "color_map.hpp"
#include <QImage>
#include <QPointF>
struct IcoAtlasPick { int face=-1,node=-1,chart=-1; double value=0; };
class IcoAtlasRenderer {
public:
  bool rectangular=false,mesh_lines=true,seams=true;
  QImage render(const Icosahedral::Result &result,const HdfColorMap::Palette &palette,const HdfColorMap::Mapping &mapping);
  IcoAtlasPick pick(int x,int y,const Icosahedral::Result &result)const;
private:
  void build(const Icosahedral::Mesh &mesh);
  int m_level=-1;
  bool m_rect=false;
  static constexpr int width=1200,height=1570;
  std::vector<int32_t> m_face;
  std::vector<float> m_bary_a,m_bary_b;
  std::vector<std::array<QPointF,3>> m_points;
};

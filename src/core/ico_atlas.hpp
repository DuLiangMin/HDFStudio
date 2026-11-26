// Five-chart indexing adapted from the existing ico_atlas.cpp; never rotates source geometry.
#pragma once
#include <QString>
#include <array>
#include <vector>
#include <cstdint>
namespace Icosahedral {
struct Mesh;
struct ChartPoint { double u=0,v=0; };
struct ChartTriangle { std::array<ChartPoint,3> uv; int chart=-1,quad=-1; };
struct Atlas {
  std::vector<ChartTriangle> triangles; // (F,3,2), same face/corner order as Mesh.
  std::vector<int32_t> node_chart; // (5,n,2n), n=2^level; bijective, excludes apex nodes.
  std::array<int32_t,2> apex{{5,6}}; // Atlas endpoints, not geographical poles in this mesh orientation.
};
QString build_atlas(const Mesh &mesh,Atlas &atlas);
}

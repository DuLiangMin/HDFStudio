// Copied/adapted five-chart parent/child indexing from HDFStudio_to_Isosahedral.
// Input: immutable native SCRIP mesh; output: UV/owner tables; no remapping or file reads.
#include "ico_atlas.hpp"
#include "icosahedral.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
namespace Icosahedral {
QString build_atlas(const Mesh &mesh,Atlas &atlas){
  if(mesh.level<0||mesh.level>6||mesh.xyz.size()<12||mesh.faces.size()!=(20ull<<(2*mesh.level)))return "Invalid five-chart mesh";
  atlas={};
  // The reference uses a rotated north ring. Reproduce that indexing on a local
  // 12-node copy, retaining the existing mesh coordinates and sampled values.
  std::array<Vec,12> base_xyz;const double angle=std::atan(1./1.6180339887498948482);
  std::vector<int> ring;
  for(int node=0;node<12;++node){auto p=mesh.xyz[node];const double y=std::cos(angle)*p[1]-std::sin(angle)*p[2];p[2]=std::sin(angle)*p[1]+std::cos(angle)*p[2];p[1]=y;base_xyz[node]=p;if(node!=5&&p[2]>0)ring.push_back(node);}
  if(ring.size()!=5)return "Invalid SCRIP north ring";
  const double spin=-std::atan2(base_xyz[ring[0]][1],base_xyz[ring[0]][0]);
  for(auto &p:base_xyz){const double x=std::cos(spin)*p[0]-std::sin(spin)*p[1];p[1]=std::sin(spin)*p[0]+std::cos(spin)*p[1];p[0]=x;}
  auto longitude=[&](int node){double a=std::atan2(base_xyz[node][1],base_xyz[node][0]);if(a< -1e-12)a+=2*3.14159265358979323846;return a;};
  std::sort(ring.begin(),ring.end(),[&](int a,int b){return longitude(a)<longitude(b);});
  const size_t per_base=size_t(1)<<(2*mesh.level);
  // Recover each parent's three original corners from its corner descendants.
  std::array<Face,20> faces;
  for(size_t f=0;f<20;++f){const size_t start=f*per_base;if(!mesh.level)faces[f]=mesh.faces[start];else faces[f]={mesh.faces[start][0],mesh.faces[start+per_base/4][0],mesh.faces[start+per_base/2][0]};}
  auto find_face=[&](int a,int b,int excluded,int required){for(int f=0;f<20;++f){const auto &v=faces[f];if(f==excluded||std::find(v.begin(),v.end(),a)==v.end()||std::find(v.begin(),v.end(),b)==v.end())continue;if(required>=0&&std::find(v.begin(),v.end(),required)==v.end())continue;return f;}return -1;};
  auto third=[&](int f,int a,int b){if(f<0)return -1;for(int v:faces[f])if(v!=a&&v!=b)return v;return -1;};
  const std::array<ChartPoint,6> corners{{{0,1},{0,0},{1,1},{1,0},{2,1},{2,0}}};const int planar[4][3]={{0,1,2},{2,1,3},{2,3,4},{4,3,5}};
  atlas.triangles.resize(mesh.faces.size());const int n=1<<mesh.level;atlas.node_chart.assign(size_t(10*n*n),-1);std::set<int> assigned;QString error;
  for(int chart=0;chart<5;++chart){
    const int u=ring[chart],next=ring[(chart+1)%5],f0=find_face(u,next,-1,5),f1=find_face(u,next,f0,-1),lower=third(f1,u,next),f2=find_face(next,lower,f1,-1),other=third(f2,next,lower),f3=find_face(lower,other,f2,6);
    if(f0<0||f1<0||f2<0||f3<0)return "Five-chart ring connectivity mismatch";
    const int ids[6]={5,u,next,lower,other,6},groups[4]={f0,f1,f2,f3};
    for(int quad=0;quad<4;++quad){const int parent=groups[quad];if(!assigned.insert(parent).second)return "Duplicate atlas base face";std::array<ChartPoint,3> triangle;
      for(int k=0;k<3;++k)for(int t=0;t<3;++t)if(faces[parent][k]==ids[planar[quad][t]])triangle[k]=corners[planar[quad][t]];
      std::function<void(int,size_t,std::array<ChartPoint,3>)> subdivide;
      subdivide=[&](int depth,size_t face,std::array<ChartPoint,3> p){
        if(!depth){atlas.triangles[face]={p,chart,quad};for(int k=0;k<3;++k){const int x=int(std::llround(p[k].u*n)),y=int(std::llround(p[k].v*n));if(x>=0&&x<2*n&&y>=0&&y<n){auto &owner=atlas.node_chart[size_t(chart*2*n*n+y*2*n+x)];const auto node=mesh.faces[face][k];if(owner!=-1&&owner!=node)error="Chart node ownership conflict";owner=node;}}return;}
        const ChartPoint ab{(p[0].u+p[1].u)/2,(p[0].v+p[1].v)/2},bc{(p[1].u+p[2].u)/2,(p[1].v+p[2].v)/2},ca{(p[2].u+p[0].u)/2,(p[2].v+p[0].v)/2};const size_t step=size_t(1)<<(2*(depth-1));
        subdivide(depth-1,face,{p[0],ab,ca});subdivide(depth-1,face+step,{p[1],bc,ab});subdivide(depth-1,face+2*step,{p[2],ca,bc});subdivide(depth-1,face+3*step,{ab,bc,ca});
      };subdivide(mesh.level,size_t(parent)*per_base,triangle);
    }
  }
  const std::set<int32_t> owners(atlas.node_chart.begin(),atlas.node_chart.end());
  if(!error.isEmpty()||owners.count(-1)||owners.count(5)||owners.count(6)||owners.size()!=atlas.node_chart.size())return "Five-chart node map is not bijective";
  return {};
}
}

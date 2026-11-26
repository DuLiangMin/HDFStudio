/* 06 SCRIP_C_Version, 2026-11-21. 本地C17派生/新几何实现。
 * 数组所有权、单位及算法边界见 ../26NOV21_接口与模块设计.md。
 * 兼容公式保留上游来源；完整声明见 ../LICENSE_SCRIP。 */
#ifndef SCRIP_C_VERSION_H
#define SCRIP_C_VERSION_H
#include <stddef.h>
#include <stdint.h>
#define SC_PI 3.141592653589793238462643383279502884
#define SC_MAX_CORNERS 16
#define SC_CLIP_VERTICES 64
enum { SC_OK=0, SC_ARGUMENT=1, SC_MEMORY=2, SC_GEOMETRY=3, SC_IO=4, SC_UNSUPPORTED=5 };
enum { SC_FRACAREA=0, SC_DESTAREA=1, SC_NONE=2 };
enum { SC_GREAT_CIRCLE=0, SC_LATITUDE_CIRCLE=1 };
typedef struct { double x,y,z; } sc_vec;
typedef struct {
    size_t count; int corners,rank,dims[2],boundary;
    double *lat,*lon,*corner_lat,*corner_lon,*area,*radius;
    sc_vec *center,*vertices; int32_t *mask;
} sc_grid;
typedef struct { size_t vertex_count,face_count; sc_vec *xyz; int32_t *faces; } sc_mesh;
typedef struct {
    size_t source_count,target_count,links,capacity; int components,normalization;
    int32_t *source,*target; double *weight;
    double *source_area,*target_area,*source_fraction,*target_fraction;
} sc_map;
typedef struct { size_t rows,columns,links; size_t *offset; int32_t *source; double *weight; } sc_csr;
typedef struct { uint64_t possible_pairs,candidates,intersections,links; double seconds; } sc_stats;
const char *sc_error(int code);
double sc_seconds(void);
sc_vec sc_add(sc_vec a,sc_vec b);
sc_vec sc_scale(sc_vec a,double value);
double sc_dot(sc_vec a,sc_vec b);
sc_vec sc_cross(sc_vec a,sc_vec b);
double sc_norm(sc_vec a);
sc_vec sc_unit(sc_vec a);
sc_vec sc_lonlat(double lon,double lat);
double sc_triangle_area(sc_vec a,sc_vec b,sc_vec c);
double sc_polygon_area(const sc_vec *vertices,int count);
int sc_clip(const sc_vec *first,int first_count,const sc_vec *second,int second_count,sc_vec *output,int *output_count);
int sc_icosphere(int level,sc_mesh *mesh);
void sc_mesh_free(sc_mesh *mesh);
int sc_grid_alloc(size_t count,int corners,sc_grid *grid);
void sc_grid_free(sc_grid *grid);
int sc_grid_prepare(sc_grid *grid);
int sc_grid_regular(int nx,int ny,int boundary,sc_grid *grid);
int sc_grid_is_regular(const sc_grid *grid);
int sc_grid_from_mesh(const sc_mesh *mesh,double angle_y,double angle_z,sc_grid *grid);
int sc_grid_read(const char *path,int boundary,sc_grid *grid);
int sc_grid_write(const char *path,const sc_grid *grid);
int sc_map_alloc(size_t source_count,size_t target_count,int components,sc_map *map);
void sc_map_free(sc_map *map);
int sc_map_append(sc_map *map,int32_t source,int32_t target,const double *weight);
int sc_map_normalize(sc_map *map,int normalization);
int sc_map_transpose(const sc_map *map,int normalization,sc_map *reverse);
int sc_map_read(const char *path,sc_map *map);
int sc_map_write(const char *path,const sc_map *map,const sc_grid *source,const sc_grid *target);
int sc_map_build(const sc_grid *source,const sc_grid *target,int normalization,int use_search,sc_map *map,sc_stats *stats);
int sc_map_regular(const sc_grid *source,const sc_grid *target,int normalization,sc_map *map,sc_stats *stats);
int sc_map_regular_reference(const sc_grid *source,const sc_grid *target,int normalization,sc_map *map,sc_stats *stats);
/* 点采样CSR独立于面积权重；源纬度范围外或四点掩膜无效时输出空行。 */
int sc_bilinear_build(const sc_grid *source,size_t target_count,const double *latitude,const double *longitude,const int32_t *mask,sc_csr *csr,sc_stats *stats);
int sc_apply_checked(const sc_map *map,const double *value,const double *gradient1,const double *gradient2,const double *gradient3,double *output);
void sc_apply_raw(size_t target_count,size_t links,int components,const int32_t *source,const int32_t *target,const double *weight,const double *value,const double *gradient1,const double *gradient2,const double *gradient3,double *output,int repeats);
int sc_csr_build(const sc_map *map,sc_csr *csr);
void sc_csr_free(sc_csr *csr);
void sc_csr_apply(const sc_csr *csr,const double *value,double *output,int repeats);
void sc_line_integral_phi(double phi1,double phi2,double theta1,double theta2,double longitude1,double longitude2,double weight[6]);
double sc_legacy_area(int corners,const double *latitude,const double *longitude);
void sc_phi_batch(int count,const double *coordinates,double *weights,int repeats);
void sc_area_batch(int count,int corners,const double *latitude,const double *longitude,double *areas,int repeats);
#endif

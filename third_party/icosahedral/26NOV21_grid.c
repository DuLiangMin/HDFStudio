/* 2026-11-21: 显式拥有网格上下文、规则单元及三角网格。 */
#include "26NOV21_scrip.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
int sc_grid_alloc(size_t count,int corners,sc_grid *grid) {
    if(!grid||!count||count>INT32_MAX||corners<3||corners>SC_MAX_CORNERS) return SC_ARGUMENT;
    memset(grid,0,sizeof(*grid));grid->count=count;grid->corners=corners;grid->rank=1;grid->dims[0]=(int)count;
    grid->lat=calloc(count,sizeof(double));grid->lon=calloc(count,sizeof(double));grid->corner_lat=calloc(count*(size_t)corners,sizeof(double));grid->corner_lon=calloc(count*(size_t)corners,sizeof(double));
    grid->area=calloc(count,sizeof(double));grid->radius=calloc(count,sizeof(double));grid->center=calloc(count,sizeof(sc_vec));grid->vertices=calloc(count*(size_t)corners,sizeof(sc_vec));grid->mask=malloc(count*sizeof(int32_t));
    if(!grid->lat||!grid->lon||!grid->corner_lat||!grid->corner_lon||!grid->area||!grid->radius||!grid->center||!grid->vertices||!grid->mask) {sc_grid_free(grid);return SC_MEMORY;}
    for(size_t index=0;index<count;++index) grid->mask[index]=1;
    return SC_OK;
}
void sc_grid_free(sc_grid *grid) {
    if(!grid)return;
    free(grid->lat);free(grid->lon);free(grid->corner_lat);free(grid->corner_lon);free(grid->area);free(grid->radius);free(grid->center);free(grid->vertices);free(grid->mask);memset(grid,0,sizeof(*grid));
}
int sc_grid_prepare(sc_grid *grid) {
    if(!grid||!grid->count||!grid->vertices) return SC_ARGUMENT;
    for(size_t cell=0;cell<grid->count;++cell) {
        if(!isfinite(grid->lat[cell])||!isfinite(grid->lon[cell])||fabs(grid->lat[cell])>SC_PI/2+1e-13||(grid->mask[cell]!=0&&grid->mask[cell]!=1))return SC_ARGUMENT;
        sc_vec *vertices=grid->vertices+cell*(size_t)grid->corners;
        sc_vec center={0,0,0};
        for(int corner=0;corner<grid->corners;++corner) {
            size_t index=cell*(size_t)grid->corners+(size_t)corner;
            if(!isfinite(grid->corner_lat[index])||!isfinite(grid->corner_lon[index])||fabs(grid->corner_lat[index])>SC_PI/2+1e-13)return SC_GEOMETRY;
            vertices[corner]=sc_lonlat(grid->corner_lon[index],grid->corner_lat[index]);center=sc_add(center,vertices[corner]);
        }
        center=sc_unit(center);if(sc_norm(center)<0.5)return SC_GEOMETRY;
        grid->center[cell]=center;
        double orientation=0;
        for(int corner=0;corner<grid->corners;++corner)orientation+=sc_dot(sc_cross(vertices[corner],vertices[(corner+1)%grid->corners]),center);
        if(orientation<0) {
            for(int corner=0;corner<grid->corners/2;++corner) {
                int other=grid->corners-1-corner;sc_vec tmp=vertices[corner];vertices[corner]=vertices[other];vertices[other]=tmp;
                size_t first=cell*(size_t)grid->corners+(size_t)corner,last=cell*(size_t)grid->corners+(size_t)other;
                double temp=grid->corner_lat[first];grid->corner_lat[first]=grid->corner_lat[last];grid->corner_lat[last]=temp;
                temp=grid->corner_lon[first];grid->corner_lon[first]=grid->corner_lon[last];grid->corner_lon[last]=temp;
            }
        }
        double radius=0;
        for(int corner=0;corner<grid->corners;++corner) {
            double dot=sc_dot(center,vertices[corner]);if(dot>1)dot=1;if(dot< -1)dot=-1;double angle=acos(dot);if(angle>radius)radius=angle;
            sc_vec normal=sc_cross(vertices[corner],vertices[(corner+1)%grid->corners]);
            for(int point=0;point<grid->corners;++point)if(sc_dot(normal,vertices[point])< -2e-12)return SC_GEOMETRY;
        }
        if(radius>=SC_PI/2-1e-12)return SC_GEOMETRY;
        grid->radius[cell]=radius;
        if(grid->boundary==SC_GREAT_CIRCLE)grid->area[cell]=sc_polygon_area(vertices,grid->corners);
        if(!isfinite(grid->area[cell])||grid->area[cell]<=1e-18) return SC_GEOMETRY;
    }
    return SC_OK;
}
int sc_grid_regular(int nx,int ny,int boundary,sc_grid *grid) {
    if(nx<4||ny<2||(boundary!=0&&boundary!=1)) return SC_ARGUMENT;
    int code=sc_grid_alloc((size_t)nx*(size_t)ny,4,grid);if(code)return code;
    grid->boundary=boundary;grid->rank=2;grid->dims[0]=nx;grid->dims[1]=ny;
    for(int row=0;row<ny;++row)for(int column=0;column<nx;++column) {
        size_t cell=(size_t)row*(size_t)nx+(size_t)column;
        double west=2*SC_PI*column/nx,east=2*SC_PI*(column+1)/nx;
        double south=-SC_PI/2+SC_PI*row/ny,north=-SC_PI/2+SC_PI*(row+1)/ny;
        double latitude[4]={south,south,north,north},longitude[4]={west,east,east,west};
        memcpy(grid->corner_lat+4*cell,latitude,sizeof(latitude));memcpy(grid->corner_lon+4*cell,longitude,sizeof(longitude));
        grid->lat[cell]=(south+north)/2;grid->lon[cell]=(west+east)/2;
        grid->area[cell]=(east-west)*(sin(north)-sin(south));
    }
    code=sc_grid_prepare(grid);if(code)sc_grid_free(grid);return code;
}
int sc_grid_is_regular(const sc_grid *grid) {
    if(!grid||!grid->corner_lat||!grid->corner_lon||!grid->area||!grid->mask||grid->corners!=4||grid->rank!=2||grid->dims[0]<4||grid->dims[1]<2||(size_t)grid->dims[0]*(size_t)grid->dims[1]!=grid->count||grid->count>INT32_MAX)return 0;
    for(int row=0;row<grid->dims[1];++row)for(int column=0;column<grid->dims[0];++column) {
        size_t cell=(size_t)row*(size_t)grid->dims[0]+(size_t)column;
        double west=2*SC_PI*column/grid->dims[0],east=2*SC_PI*(column+1)/grid->dims[0];
        double south=-SC_PI/2+SC_PI*row/grid->dims[1],north=-SC_PI/2+SC_PI*(row+1)/grid->dims[1];
        double lat[4]={south,south,north,north},lon[4]={west,east,east,west};
        if(!isfinite(grid->area[cell])||grid->area[cell]<=0||(grid->mask[cell]!=0&&grid->mask[cell]!=1))return 0;
        double exact_area=(east-west)*(sin(north)-sin(south));
        if(grid->boundary==SC_LATITUDE_CIRCLE&&fabs(grid->area[cell]-exact_area)>1e-11*exact_area)return 0;
        for(int corner=0;corner<4;++corner) {
            size_t index=4*cell+(size_t)corner;
            if(!isfinite(grid->corner_lat[index])||!isfinite(grid->corner_lon[index])||fabs(grid->corner_lat[index]-lat[corner])>5e-12||fabs(grid->corner_lon[index]-lon[corner])>5e-12)return 0;
            /* 快捷算法要求张量积轴严格一致；拒绝轻微曲线网格。 */
            if(grid->corner_lat[index]!=grid->corner_lat[4*(size_t)row*(size_t)grid->dims[0]+(size_t)corner]||grid->corner_lon[index]!=grid->corner_lon[4*(size_t)column+(size_t)corner])return 0;
        }
    }
    return 1;
}
int sc_grid_from_mesh(const sc_mesh *mesh,double angle_y,double angle_z,sc_grid *grid) {
    if(!mesh||!mesh->face_count||!mesh->xyz||!mesh->faces||!isfinite(angle_y)||!isfinite(angle_z))return SC_ARGUMENT;
    for(size_t index=0;index<mesh->face_count*3;++index)if(mesh->faces[index]<0||(size_t)mesh->faces[index]>=mesh->vertex_count)return SC_ARGUMENT;
    int code=sc_grid_alloc(mesh->face_count,3,grid);if(code)return code;
    for(size_t face=0;face<mesh->face_count;++face) {
        sc_vec sum={0,0,0};
        for(int corner=0;corner<3;++corner) {
            sc_vec point=mesh->xyz[mesh->faces[3*face+(size_t)corner]];
            sc_vec rotated={cos(angle_y)*point.x+sin(angle_y)*point.z,point.y,-sin(angle_y)*point.x+cos(angle_y)*point.z};
            point=(sc_vec){cos(angle_z)*rotated.x-sin(angle_z)*rotated.y,sin(angle_z)*rotated.x+cos(angle_z)*rotated.y,rotated.z};
            size_t index=3*face+(size_t)corner;grid->corner_lat[index]=asin(fmax(-1,fmin(1,point.z)));grid->corner_lon[index]=atan2(point.y,point.x);sum=sc_add(sum,point);
        }
        sum=sc_unit(sum);grid->lat[face]=asin(sum.z);grid->lon[face]=atan2(sum.y,sum.x);
    }
    code=sc_grid_prepare(grid);if(code)sc_grid_free(grid);return code;
}

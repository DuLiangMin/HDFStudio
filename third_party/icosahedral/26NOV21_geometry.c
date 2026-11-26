/* 2026-11-21: 单位球向量/凸大圆裁剪。新后端，不是04梯形积分的逐位翻译。 */
#include "26NOV21_scrip.h"
#include <math.h>
#include <string.h>
#include <time.h>
const char *sc_error(int code) {
    const char *names[]={"success","invalid argument or index","allocation failed","invalid or unsupported geometry","NetCDF I/O failure","unsupported operation"};
    return code>=0 && code<=5?names[code]:"unknown error";
}
double sc_seconds(void) { struct timespec value; timespec_get(&value,TIME_UTC); return (double)value.tv_sec+1e-9*(double)value.tv_nsec; }
sc_vec sc_add(sc_vec a,sc_vec b) { return (sc_vec){a.x+b.x,a.y+b.y,a.z+b.z}; }
sc_vec sc_scale(sc_vec a,double value) { return (sc_vec){a.x*value,a.y*value,a.z*value}; }
double sc_dot(sc_vec a,sc_vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
sc_vec sc_cross(sc_vec a,sc_vec b) { return (sc_vec){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
double sc_norm(sc_vec a) { return sqrt(sc_dot(a,a)); }
sc_vec sc_unit(sc_vec a) { double norm=sc_norm(a); return norm>0?sc_scale(a,1/norm):(sc_vec){0,0,0}; }
sc_vec sc_lonlat(double lon,double lat) { return (sc_vec){cos(lat)*cos(lon),cos(lat)*sin(lon),sin(lat)}; }
double sc_triangle_area(sc_vec a,sc_vec b,sc_vec c) {
    return 2*atan2(fabs(sc_dot(a,sc_cross(b,c))),1+sc_dot(a,b)+sc_dot(b,c)+sc_dot(c,a));
}
double sc_polygon_area(const sc_vec *vertices,int count) {
    double area=0; for(int index=1;index+1<count;++index) area+=sc_triangle_area(vertices[0],vertices[index],vertices[index+1]);
    return area;
}
static int push(sc_vec *vertices,int *count,sc_vec point) {
    if(*count && sc_norm(sc_add(vertices[*count-1],sc_scale(point,-1)))<2e-13) return SC_OK;
    if(*count>=SC_CLIP_VERTICES) return SC_GEOMETRY;
    vertices[(*count)++]=point; return SC_OK;
}
int sc_clip(const sc_vec *first,int first_count,const sc_vec *second,int second_count,sc_vec *output,int *output_count) {
    if(!first||!second||!output||!output_count||first_count<3||second_count<3||first_count>SC_MAX_CORNERS||second_count>SC_MAX_CORNERS) return SC_ARGUMENT;
    sc_vec buffers[2][SC_CLIP_VERTICES]; int count=first_count,current=0;
    memcpy(buffers[0],first,(size_t)count*sizeof(sc_vec));
    for(int edge=0;edge<second_count && count>=3;++edge) {
        sc_vec cross=sc_cross(second[edge],second[(edge+1)%second_count]);
        /* 极点重复角点表示零长边，没有边界约束，不产生局部/原边号混用。 */
        if(sc_norm(cross)<1e-14) continue;
        sc_vec normal=sc_unit(cross);
        int next=1-current,new_count=0;
        sc_vec previous=buffers[current][count-1]; double d_previous=sc_dot(normal,previous);
        for(int index=0;index<count;++index) {
            sc_vec point=buffers[current][index]; double distance=sc_dot(normal,point);
            int inside=distance>=-2e-15,previous_inside=d_previous>=-2e-15;
            if(inside!=previous_inside) {
                double divisor=d_previous-distance;
                if(fabs(divisor)>1e-30) {
                    /* 弦与平面的交点单位化，得到同一短大圆弧上的交点。 */
                    double fraction=d_previous/divisor;
                    if(fraction<0) fraction=0;
                    if(fraction>1) fraction=1;
                    sc_vec intersection=sc_unit(sc_add(sc_scale(previous,1-fraction),sc_scale(point,fraction)));
                    int code=push(buffers[next],&new_count,intersection); if(code) return code;
                }
            }
            if(inside) { int code=push(buffers[next],&new_count,point); if(code) return code; }
            previous=point; d_previous=distance;
        }
        if(new_count>1 && sc_norm(sc_add(buffers[next][0],sc_scale(buffers[next][new_count-1],-1)))<2e-13) --new_count;
        current=next;count=new_count;
    }
    *output_count=count>=3?count:0;
    memcpy(output,buffers[current],(size_t)*output_count*sizeof(sc_vec)); return SC_OK;
}

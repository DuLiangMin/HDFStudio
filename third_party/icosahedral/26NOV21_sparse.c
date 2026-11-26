/* 2026-11-21: SCRIP remap_mod C派生及独立CSR应用、显式归一化。
 * Copyright (c) 1997, 1998 the Regents of the University of California.
 * 完整Notice见 ../LICENSE_SCRIP；修改版，不是Los Alamos原版。 */
#include "26NOV21_scrip.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
int sc_map_alloc(size_t source_count,size_t target_count,int components,sc_map *map) {
    if(!map||!source_count||!target_count||source_count>INT32_MAX||target_count>INT32_MAX||(components!=1&&components!=3&&components!=4))return SC_ARGUMENT;
    memset(map,0,sizeof(*map));map->source_count=source_count;map->target_count=target_count;map->components=components;map->normalization=SC_NONE;
    map->source_area=calloc(source_count,sizeof(double));map->target_area=calloc(target_count,sizeof(double));
    map->source_fraction=calloc(source_count,sizeof(double));map->target_fraction=calloc(target_count,sizeof(double));
    if(!map->source_area||!map->target_area||!map->source_fraction||!map->target_fraction){sc_map_free(map);return SC_MEMORY;}return SC_OK;
}
void sc_map_free(sc_map *map) {if(!map)return;free(map->source);free(map->target);free(map->weight);free(map->source_area);free(map->target_area);free(map->source_fraction);free(map->target_fraction);memset(map,0,sizeof(*map));}
int sc_map_append(sc_map *map,int32_t source,int32_t target,const double *weight) {
    if(!map||!weight||source<0||target<0||(size_t)source>=map->source_count||(size_t)target>=map->target_count)return SC_ARGUMENT;
    for(int component=0;component<map->components;++component)if(!isfinite(weight[component]))return SC_ARGUMENT;
    if(map->links==map->capacity) {
        size_t capacity=map->capacity?2*map->capacity:256;
        if(capacity>SIZE_MAX/((size_t)map->components*sizeof(double)))return SC_MEMORY;
        int32_t *s=realloc(map->source,capacity*sizeof(int32_t));if(!s)return SC_MEMORY;map->source=s;
        int32_t *t=realloc(map->target,capacity*sizeof(int32_t));if(!t)return SC_MEMORY;map->target=t;
        double *w=realloc(map->weight,capacity*(size_t)map->components*sizeof(double));if(!w)return SC_MEMORY;map->weight=w;map->capacity=capacity;
    }
    map->source[map->links]=source;map->target[map->links]=target;
    memcpy(map->weight+map->links*(size_t)map->components,weight,(size_t)map->components*sizeof(double));++map->links;return SC_OK;
}
static double target_measure(const sc_map *map,size_t target) {
    if(map->normalization==SC_FRACAREA)return map->target_area[target]*map->target_fraction[target];
    if(map->normalization==SC_DESTAREA)return map->target_area[target];
    return 1;
}
int sc_map_normalize(sc_map *map,int normalization) {
    if(!map||normalization<0||normalization>2)return SC_ARGUMENT;
    if(map->normalization<0||map->normalization>2||(map->components!=1&&map->components!=3&&map->components!=4))return SC_ARGUMENT;
    for(size_t cell=0;cell<map->source_count;++cell)if(!isfinite(map->source_area[cell])||map->source_area[cell]<=0)return SC_ARGUMENT;
    for(size_t cell=0;cell<map->target_count;++cell)if(!isfinite(map->target_area[cell])||map->target_area[cell]<=0)return SC_ARGUMENT;
    for(size_t link=0;link<map->links;++link) {
        if(map->source[link]<0||map->target[link]<0||(size_t)map->source[link]>=map->source_count||(size_t)map->target[link]>=map->target_count)return SC_ARGUMENT;
        for(int c=0;c<map->components;++c)if(!isfinite(map->weight[link*(size_t)map->components+(size_t)c]))return SC_ARGUMENT;
    }
    if(map->normalization!=SC_NONE) {
        for(size_t link=0;link<map->links;++link)for(int c=0;c<map->components;++c)map->weight[link*(size_t)map->components+(size_t)c]*=target_measure(map,(size_t)map->target[link]);
    }
    memset(map->source_fraction,0,map->source_count*sizeof(double));memset(map->target_fraction,0,map->target_count*sizeof(double));
    for(size_t link=0;link<map->links;++link) {
        double area=map->weight[link*(size_t)map->components];map->source_fraction[map->source[link]]+=area;map->target_fraction[map->target[link]]+=area;
    }
    for(size_t cell=0;cell<map->source_count;++cell)map->source_fraction[cell]/=map->source_area[cell];
    for(size_t cell=0;cell<map->target_count;++cell)map->target_fraction[cell]/=map->target_area[cell];
    map->normalization=normalization;
    for(size_t link=0;link<map->links;++link) {
        double divisor=target_measure(map,(size_t)map->target[link]);
        /* 上游文件可保留零覆盖、全部分量为零的链接；保留空链接，禁止0/0。 */
        if(divisor==0) {
            for(int c=0;c<map->components;++c)if(map->weight[link*(size_t)map->components+(size_t)c]!=0)return SC_GEOMETRY;
            continue;
        }
        if(!isfinite(divisor)||divisor<0)return SC_GEOMETRY;
        for(int c=0;c<map->components;++c)map->weight[link*(size_t)map->components+(size_t)c]/=divisor;
    }
    return SC_OK;
}
int sc_map_transpose(const sc_map *map,int normalization,sc_map *reverse) {
    /* 梯度矩以源中心为参照，不能直接转置，故明确只支持一阶。 */
    if(!map||map->components!=1)return SC_UNSUPPORTED;
    int code=sc_map_alloc(map->target_count,map->source_count,1,reverse);if(code)return code;
    memcpy(reverse->source_area,map->target_area,map->target_count*sizeof(double));memcpy(reverse->target_area,map->source_area,map->source_count*sizeof(double));
    for(size_t link=0;link<map->links;++link) {
        double area=map->weight[link]*target_measure(map,(size_t)map->target[link]);
        code=sc_map_append(reverse,map->target[link],map->source[link],&area);if(code){sc_map_free(reverse);return code;}
    }
    code=sc_map_normalize(reverse,normalization);if(code)sc_map_free(reverse);return code;
}
void sc_apply_raw(size_t target_count,size_t links,int components,const int32_t *source,const int32_t *target,const double *weight,const double *value,const double *g1,const double *g2,const double *g3,double *output,int repeats) {
    for(int repeat=0;repeat<repeats;++repeat) {
        memset(output,0,target_count*sizeof(double));
        if(!g1) {for(size_t link=0;link<links;++link)output[target[link]]+=value[source[link]]*weight[link*(size_t)components];}
        else if(components==3) {for(size_t link=0;link<links;++link){int32_t s=source[link],t=target[link];const double *w=weight+3*link;output[t]=output[t]+value[s]*w[0]+g1[s]*w[1]+g2[s]*w[2];}}
        else if(components==4) {for(size_t link=0;link<links;++link){int32_t s=source[link],t=target[link];const double *w=weight+4*link;output[t]=output[t]+value[s]*w[0]+g1[s]*w[1]+g2[s]*w[2]+g3[s]*w[3];}}
    }
}
int sc_apply_checked(const sc_map *map,const double *value,const double *g1,const double *g2,const double *g3,double *output) {
    if(!map||!value||!output||output==value||output==g1||output==g2||output==g3||!map->source_count||!map->target_count||(map->components!=1&&map->components!=3&&map->components!=4)||(g1&&((map->components!=3&&map->components!=4)||!g2))||(g1&&map->components==4&&!g3)||(!g1&&(g2||g3)))return SC_ARGUMENT;
    for(size_t cell=0;cell<map->source_count;++cell)if(!isfinite(value[cell])||(g1&&(!isfinite(g1[cell])||!isfinite(g2[cell])||(g3&&!isfinite(g3[cell])))))return SC_ARGUMENT;
    for(size_t link=0;link<map->links;++link) {
        if(map->source[link]<0||map->target[link]<0||(size_t)map->source[link]>=map->source_count||(size_t)map->target[link]>=map->target_count)return SC_ARGUMENT;
        for(int c=0;c<map->components;++c)if(!isfinite(map->weight[link*(size_t)map->components+(size_t)c]))return SC_ARGUMENT;
    }
    sc_apply_raw(map->target_count,map->links,map->components,map->source,map->target,map->weight,value,g1,g2,g3,output,1);return SC_OK;
}
int sc_csr_build(const sc_map *map,sc_csr *csr) {
    if(!map||!csr)return SC_ARGUMENT;
    memset(csr,0,sizeof(*csr));csr->rows=map->target_count;csr->columns=map->source_count;csr->links=map->links;
    csr->offset=calloc(csr->rows+1,sizeof(size_t));csr->source=malloc((map->links+1)*sizeof(int32_t));csr->weight=malloc((map->links+1)*sizeof(double));
    if(!csr->offset||!csr->source||!csr->weight){sc_csr_free(csr);return SC_MEMORY;}
    for(size_t link=0;link<map->links;++link){if(map->source[link]<0||map->target[link]<0||(size_t)map->source[link]>=csr->columns||(size_t)map->target[link]>=csr->rows){sc_csr_free(csr);return SC_ARGUMENT;}++csr->offset[(size_t)map->target[link]+1];}
    for(size_t row=0;row<csr->rows;++row)csr->offset[row+1]+=csr->offset[row];
    size_t *cursor=malloc((csr->rows+1)*sizeof(size_t));if(!cursor){sc_csr_free(csr);return SC_MEMORY;}memcpy(cursor,csr->offset,(csr->rows+1)*sizeof(size_t));
    for(size_t link=0;link<map->links;++link){size_t position=cursor[map->target[link]]++;csr->source[position]=map->source[link];csr->weight[position]=map->weight[link*(size_t)map->components];}free(cursor);return SC_OK;
}
void sc_csr_free(sc_csr *csr){if(!csr)return;free(csr->offset);free(csr->source);free(csr->weight);memset(csr,0,sizeof(*csr));}
void sc_csr_apply(const sc_csr *csr,const double *value,double *output,int repeats) {
    for(int repeat=0;repeat<repeats;++repeat)for(size_t row=0;row<csr->rows;++row){double sum=0;for(size_t link=csr->offset[row];link<csr->offset[row+1];++link)sum+=csr->weight[link]*value[csr->source[link]];output[row]=sum;}
}

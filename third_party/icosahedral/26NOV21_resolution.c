/* 2026-11-21: 粗细网格转换；规则纬圈交叠扫描及点值双线性采样。
 * 四点公式来源于SCRIP remap_bilinear.f，规则轴定位是本地优化。
 * Copyright (c) 1997, 1998 the Regents of the University of California.
 * 完整Notice见 ../LICENSE_SCRIP；标记为修改/派生实现，不是上游原版。
 * 接口、覆盖策略、大小与所有权见 ../26NOV21_分辨率转换接口.md。 */
#include "26NOV21_scrip.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int source, target;
    double measure;
} axis_overlap;

static int merge_axis(const sc_grid *source,const sc_grid *target,int latitude,axis_overlap **output,size_t *count) {
    int source_size=source->dims[latitude],target_size=target->dims[latitude];
    size_t capacity=(size_t)source_size+(size_t)target_size;
    axis_overlap *overlap=malloc(capacity*sizeof(*overlap));
    if(!overlap)return SC_MEMORY;
    int src=0,dst=0;size_t used=0;
    while(src<source_size&&dst<target_size) {
        size_t source_offset=latitude?(size_t)src*(size_t)source->dims[0]*4:(size_t)src*4;
        size_t target_offset=latitude?(size_t)dst*(size_t)target->dims[0]*4:(size_t)dst*4;
        const double *source_axis=latitude?source->corner_lat:source->corner_lon;
        const double *target_axis=latitude?target->corner_lat:target->corner_lon;
        int upper_corner=latitude?2:1;
        double source_end=source_axis[source_offset+(size_t)upper_corner];
        double target_end=target_axis[target_offset+(size_t)upper_corner];
        double lower=fmax(source_axis[source_offset],target_axis[target_offset]);
        double upper=fmin(source_end,target_end);
        if(upper>lower) {
            overlap[used].source=src;overlap[used].target=dst;
            overlap[used].measure=latitude?sin(upper)-sin(lower):upper-lower;
            ++used;
        }
        if(source_end<=target_end)++src;
        if(target_end<=source_end)++dst;
    }
    *output=overlap;*count=used;return SC_OK;
}

int sc_map_regular(const sc_grid *source,const sc_grid *target,int normalization,sc_map *map,sc_stats *stats) {
    if(!source||!target||!map||!stats)return SC_ARGUMENT;
    if(normalization<SC_FRACAREA||normalization>SC_NONE)return SC_ARGUMENT;
    if(source->boundary!=SC_LATITUDE_CIRCLE||target->boundary!=SC_LATITUDE_CIRCLE||!sc_grid_is_regular(source)||!sc_grid_is_regular(target))return SC_UNSUPPORTED;
    double start=sc_seconds();memset(stats,0,sizeof(*stats));
    stats->possible_pairs=(uint64_t)source->count*(uint64_t)target->count;
    axis_overlap *longitude=NULL,*latitude=NULL;size_t longitude_count=0,latitude_count=0;
    int code=merge_axis(source,target,0,&longitude,&longitude_count);
    if(!code)code=merge_axis(source,target,1,&latitude,&latitude_count);
    if(!code)code=sc_map_alloc(source->count,target->count,1,map);
    if(code){free(longitude);free(latitude);return code;}
    memcpy(map->source_area,source->area,source->count*sizeof(double));
    memcpy(map->target_area,target->area,target->count*sizeof(double));
    for(size_t row=0;row<latitude_count&&!code;++row) {
        for(size_t column=0;column<longitude_count;++column) {
            size_t src=(size_t)latitude[row].source*(size_t)source->dims[0]+(size_t)longitude[column].source;
            size_t dst=(size_t)latitude[row].target*(size_t)target->dims[0]+(size_t)longitude[column].target;
            if(!source->mask[src]||!target->mask[dst])continue;
            double area=longitude[column].measure*latitude[row].measure;
            ++stats->candidates;++stats->intersections;
            code=sc_map_append(map,(int32_t)src,(int32_t)dst,&area);
            if(code)break;
        }
    }
    free(longitude);free(latitude);
    if(!code)code=sc_map_normalize(map,normalization);
    stats->links=map->links;stats->seconds=sc_seconds()-start;
    if(code)sc_map_free(map);
    return code;
}

static int sampling_axes_valid(const sc_grid *source) {
    if(!source||source->rank!=2||source->dims[0]<4||source->dims[1]<2||!source->lat||!source->lon||!source->mask||source->count>INT32_MAX||(size_t)source->dims[0]*(size_t)source->dims[1]!=source->count)return 0;
    int nx=source->dims[0],ny=source->dims[1];
    if(!isfinite(source->lon[0])||fabs(source->lon[0])>4*SC_PI)return 0;
    for(int row=0;row<ny;++row) {
        double latitude=source->lat[(size_t)row*(size_t)nx];
        if(!isfinite(latitude)||fabs(latitude)>SC_PI/2||(row&&latitude<=source->lat[(size_t)(row-1)*(size_t)nx]))return 0;
        for(int column=0;column<nx;++column) {
            size_t index=(size_t)row*(size_t)nx+(size_t)column;
            double longitude=source->lon[0]+2*SC_PI*column/nx;
            if(!isfinite(source->lon[index])||source->lat[index]!=latitude||fabs(source->lon[index]-longitude)>5e-12||(source->mask[index]!=0&&source->mask[index]!=1))return 0;
        }
    }
    return 1;
}

static int lower_latitude(const sc_grid *source,double latitude) {
    int lower=0,upper=source->dims[1]-1,nx=source->dims[0];
    while(upper-lower>1) {
        int middle=lower+(upper-lower)/2;
        if(source->lat[(size_t)middle*(size_t)nx]<=latitude)lower=middle;
        else upper=middle;
    }
    return lower;
}

int sc_bilinear_build(const sc_grid *source,size_t target_count,const double *latitude,const double *longitude,const int32_t *mask,sc_csr *csr,sc_stats *stats) {
    if(!csr||!stats||!target_count||target_count>INT32_MAX||!latitude||!longitude||!mask)return SC_ARGUMENT;
    if(!sampling_axes_valid(source))return SC_UNSUPPORTED;
    for(size_t index=0;index<target_count;++index)if(!isfinite(latitude[index])||!isfinite(longitude[index])||fabs(latitude[index])>SC_PI/2||fabs(longitude[index])>4*SC_PI||(mask[index]!=0&&mask[index]!=1))return SC_ARGUMENT;
    if(target_count>SIZE_MAX/(4*sizeof(double)))return SC_MEMORY;
    double start=sc_seconds();memset(stats,0,sizeof(*stats));memset(csr,0,sizeof(*csr));
    csr->rows=target_count;csr->columns=source->count;
    csr->offset=calloc(target_count+1,sizeof(size_t));
    csr->source=malloc(4*target_count*sizeof(int32_t));
    csr->weight=malloc(4*target_count*sizeof(double));
    if(!csr->offset||!csr->source||!csr->weight){sc_csr_free(csr);return SC_MEMORY;}
    int nx=source->dims[0],ny=source->dims[1];
    double south=source->lat[0],north=source->lat[(size_t)(ny-1)*(size_t)nx];
    stats->possible_pairs=(uint64_t)source->count*(uint64_t)target_count;
    for(size_t dst=0;dst<target_count;++dst) {
        csr->offset[dst]=csr->links;
        if(!mask[dst]||latitude[dst]<south||latitude[dst]>north)continue;
        int row=lower_latitude(source,latitude[dst]);
        double longitude_offset=fmod(longitude[dst]-source->lon[0],2*SC_PI);
        if(longitude_offset<0)longitude_offset+=2*SC_PI;
        double x=longitude_offset*nx/(2*SC_PI);
        int column=(int)floor(x);
        if(column>=nx){column=0;x=0;}
        double i=x-column;
        double lower=source->lat[(size_t)row*(size_t)nx],upper=source->lat[(size_t)(row+1)*(size_t)nx];
        double j=(latitude[dst]-lower)/(upper-lower);
        int next=(column+1)%nx;
        int32_t address[4]={(int32_t)(row*nx+column),(int32_t)(row*nx+next),(int32_t)((row+1)*nx+next),(int32_t)((row+1)*nx+column)};
        double weight[4]={(1-i)*(1-j),i*(1-j),i*j,(1-i)*j};
        stats->candidates+=4;
        if(!source->mask[address[0]]||!source->mask[address[1]]||!source->mask[address[2]]||!source->mask[address[3]])continue;
        for(int corner=0;corner<4;++corner) {
            csr->source[csr->links]=address[corner];csr->weight[csr->links]=weight[corner];++csr->links;
        }
        stats->intersections+=4;
    }
    csr->offset[target_count]=csr->links;stats->links=csr->links;stats->seconds=sc_seconds()-start;
    return SC_OK;
}

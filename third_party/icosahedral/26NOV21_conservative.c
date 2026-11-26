/* 2026-11-21: 一阶几何交叠/候选分箱；新的显式上下文，拒绝共享SAVE状态。 */
#include "26NOV21_scrip.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
enum { BINS=96 };
static int bin_id(double z) {int bin=(int)((z+1)*0.5*BINS);return bin<0?0:(bin>=BINS?BINS-1:bin);}
static void cap_bounds(sc_vec center,double radius,double *lower,double *upper) {
    double latitude=asin(fmax(-1,fmin(1,center.z)));
    *lower=sin(fmax(-SC_PI/2,latitude-radius));*upper=sin(fmin(SC_PI/2,latitude+radius));
}
static int raw_map(const sc_grid *source,const sc_grid *target,sc_map *map) {
    int code=sc_map_alloc(source->count,target->count,1,map);if(code)return code;
    memcpy(map->source_area,source->area,source->count*sizeof(double));memcpy(map->target_area,target->area,target->count*sizeof(double));return SC_OK;
}
int sc_map_build(const sc_grid *source,const sc_grid *target,int normalization,int use_search,sc_map *map,sc_stats *stats) {
    if(!source||!target||!map||!stats||normalization<0||normalization>2)return SC_ARGUMENT;
    if(source->boundary!=SC_GREAT_CIRCLE||target->boundary!=SC_GREAT_CIRCLE)return SC_UNSUPPORTED;
    double start=sc_seconds();memset(stats,0,sizeof(*stats));stats->possible_pairs=(uint64_t)source->count*(uint64_t)target->count;
    int code=raw_map(source,target,map);if(code)return code;
    size_t counts[BINS]={0},offset[BINS+1]={0},cursor[BINS]={0};
    for(size_t cell=0;cell<target->count;++cell)if(target->mask[cell]){double lower,upper;cap_bounds(target->center[cell],target->radius[cell],&lower,&upper);for(int bin=bin_id(lower);bin<=bin_id(upper);++bin)++counts[bin];}
    for(int bin=0;bin<BINS;++bin)offset[bin+1]=offset[bin]+counts[bin];
    memcpy(cursor,offset,sizeof(cursor));
    int32_t *items=malloc((offset[BINS]+1)*sizeof(int32_t));int32_t *seen=malloc(target->count*sizeof(int32_t));
    if(!items||!seen){free(items);free(seen);sc_map_free(map);return SC_MEMORY;}
    for(size_t cell=0;cell<target->count;++cell)seen[cell]=-1;
    for(size_t cell=0;cell<target->count;++cell)if(target->mask[cell]){double lower,upper;cap_bounds(target->center[cell],target->radius[cell],&lower,&upper);for(int bin=bin_id(lower);bin<=bin_id(upper);++bin)items[cursor[bin]++]=(int32_t)cell;}
    for(size_t src=0;src<source->count&&!code;++src) {
        if(!source->mask[src])continue;
        double lower,upper;cap_bounds(source->center[src],source->radius[src],&lower,&upper);
        int first_bin=use_search?bin_id(lower):0,last_bin=use_search?bin_id(upper):0;
        for(int bin=first_bin;bin<=last_bin&&!code;++bin) {
            size_t begin=use_search?offset[bin]:0,end=use_search?offset[bin+1]:target->count;
            for(size_t item=begin;item<end;++item) {
                size_t dst=use_search?(size_t)items[item]:item;
                if(!target->mask[dst]||seen[dst]==(int32_t)src)continue;
                seen[dst]=(int32_t)src;
                ++stats->candidates;
                double radius=source->radius[src]+target->radius[dst]+1e-12;
                if(use_search&&sc_dot(source->center[src],target->center[dst])<cos(radius))continue;
                ++stats->intersections;
                sc_vec polygon[SC_CLIP_VERTICES];int count=0;
                code=sc_clip(source->vertices+src*(size_t)source->corners,source->corners,target->vertices+dst*(size_t)target->corners,target->corners,polygon,&count);
                if(code)break;
                double area=sc_polygon_area(polygon,count);
                /* 仅丢弃舍入量级接触；门槛随两单元较小面积缩放。 */
                if(area>1e-13*fmin(source->area[src],target->area[dst])){code=sc_map_append(map,(int32_t)src,(int32_t)dst,&area);if(code)break;}
            }
        }
    }
    free(items);free(seen);if(!code)code=sc_map_normalize(map,normalization);
    stats->links=map->links;stats->seconds=sc_seconds()-start;if(code)sc_map_free(map);return code;
}
/* 保留2026-11-21首次06扫描实现，作为优化的独立全扫描对照。 */
int sc_map_regular_reference(const sc_grid *source,const sc_grid *target,int normalization,sc_map *map,sc_stats *stats) {
    if(!source||!target||!map||!stats||source->boundary!=SC_LATITUDE_CIRCLE||target->boundary!=SC_LATITUDE_CIRCLE||source->rank!=2||target->rank!=2||source->dims[0]<4||target->dims[0]<4)return SC_UNSUPPORTED;
    if(normalization<0||normalization>2)return SC_ARGUMENT;
    if(!sc_grid_is_regular(source)||!sc_grid_is_regular(target))return SC_UNSUPPORTED;
    double start=sc_seconds();memset(stats,0,sizeof(*stats));stats->possible_pairs=(uint64_t)source->count*(uint64_t)target->count;
    int code=raw_map(source,target,map);if(code)return code;
    int sx=source->dims[0],sy=source->dims[1],tx=target->dims[0],ty=target->dims[1];
    for(int sr=0;sr<sy&&!code;++sr)for(int tr=0;tr<ty&&!code;++tr) {
        double south=fmax(source->corner_lat[(size_t)sr*(size_t)sx*4],target->corner_lat[(size_t)tr*(size_t)tx*4]);
        double north=fmin(source->corner_lat[(size_t)sr*(size_t)sx*4+2],target->corner_lat[(size_t)tr*(size_t)tx*4+2]);
        if(north<=south)continue;
        for(int sc=0;sc<sx&&!code;++sc)for(int tc=0;tc<tx&&!code;++tc) {
            size_t src=(size_t)sr*(size_t)sx+(size_t)sc,dst=(size_t)tr*(size_t)tx+(size_t)tc;if(!source->mask[src]||!target->mask[dst])continue;
            double west=fmax(source->corner_lon[4*src],target->corner_lon[4*dst]),east=fmin(source->corner_lon[4*src+1],target->corner_lon[4*dst+1]);++stats->candidates;
            if(east>west){double area=(east-west)*(sin(north)-sin(south));++stats->intersections;code=sc_map_append(map,(int32_t)src,(int32_t)dst,&area);}
        }
    }
    if(!code)code=sc_map_normalize(map,normalization);
    stats->links=map->links;stats->seconds=sc_seconds()-start;if(code)sc_map_free(map);return code;
}

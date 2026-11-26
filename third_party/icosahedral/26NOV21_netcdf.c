/* 2026-11-21: SCRIP NetCDF边界适配，单位/权重轴/一基地址显式核验。 */
#include "26NOV21_scrip.h"
#include <netcdf.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
static int dimension(int file,const char *name,size_t *length) {int id;return nc_inq_dimid(file,name,&id)||nc_inq_dimlen(file,id,length);}
static int read_vector(int file,const char *name,size_t count,double *values) {
    int variable,rank,dims[NC_MAX_VAR_DIMS];size_t length;
    if(nc_inq_varid(file,name,&variable)||nc_inq_varndims(file,variable,&rank)||rank!=1||nc_inq_vardimid(file,variable,dims)||nc_inq_dimlen(file,dims[0],&length)||length!=count)return SC_IO;
    return nc_get_var_double(file,variable,values)?SC_IO:SC_OK;
}
static int read_integer(int file,const char *name,size_t count,int32_t *values) {
    int variable,rank,dims[NC_MAX_VAR_DIMS];size_t length;nc_type type;
    if(nc_inq_varid(file,name,&variable)||nc_inq_varndims(file,variable,&rank)||rank!=1||nc_inq_vardimid(file,variable,dims)||nc_inq_dimlen(file,dims[0],&length)||length!=count)return SC_IO;
    if(nc_inq_vartype(file,variable,&type)||(type!=NC_BYTE&&type!=NC_SHORT&&type!=NC_INT))return SC_ARGUMENT;
    return nc_get_var_int(file,variable,values)?SC_IO:SC_OK;
}
static int read_angle(int file,const char *name,size_t count,double *values) {
    int variable,rank,dims[NC_MAX_VAR_DIMS];size_t length=1,attribute_length;
    if(nc_inq_varid(file,name,&variable)||nc_inq_varndims(file,variable,&rank)||rank<1||rank>2||nc_inq_vardimid(file,variable,dims))return SC_IO;
    char first[NC_MAX_NAME+1],second[NC_MAX_NAME+1];
    if(nc_inq_dimname(file,dims[0],first)||strcmp(first,"grid_size"))return SC_IO;
    if(rank==2&&(nc_inq_dimname(file,dims[1],second)||strcmp(second,"grid_corners")))return SC_IO;
    if((strstr(name,"corner")!=NULL)!=(rank==2))return SC_IO;
    for(int axis=0;axis<rank;++axis){size_t axis_length;if(nc_inq_dimlen(file,dims[axis],&axis_length)||!axis_length||length>SIZE_MAX/axis_length)return SC_IO;length*=axis_length;}
    if(length!=count||nc_inq_attlen(file,variable,"units",&attribute_length)||attribute_length>63)return SC_IO;
    char units[64]={0};if(nc_get_att_text(file,variable,"units",units)||nc_get_var_double(file,variable,values))return SC_IO;
    double factor;if(strstr(units,"degree"))factor=SC_PI/180;else if(strstr(units,"radian"))factor=1;else return SC_ARGUMENT;
    for(size_t index=0;index<count;++index){values[index]*=factor;if(!isfinite(values[index]))return SC_ARGUMENT;}return SC_OK;
}
int sc_grid_read(const char *path,int boundary,sc_grid *grid) {
    if(!path||!grid||(boundary!=0&&boundary!=1))return SC_ARGUMENT;
    memset(grid,0,sizeof(*grid));int file=-1,code=SC_IO;size_t count,corners,rank;
    if(nc_open(path,NC_NOWRITE,&file))return SC_IO;
    if(dimension(file,"grid_size",&count)||dimension(file,"grid_corners",&corners)||dimension(file,"grid_rank",&rank)||rank<1||rank>2||corners>SC_MAX_CORNERS)goto finish;
    code=sc_grid_alloc(count,(int)corners,grid);if(code)goto finish;grid->rank=(int)rank;grid->boundary=boundary;
    code=SC_IO;
    if(read_integer(file,"grid_dims",rank,grid->dims))goto finish;
    if(grid->dims[0]<=0||(rank==2&&grid->dims[1]<=0)||(size_t)grid->dims[0]*(rank==2?(size_t)grid->dims[1]:1)!=count)goto finish;
    if(read_angle(file,"grid_center_lat",count,grid->lat)||read_angle(file,"grid_center_lon",count,grid->lon)||read_angle(file,"grid_corner_lat",count*corners,grid->corner_lat)||read_angle(file,"grid_corner_lon",count*corners,grid->corner_lon))goto finish;
    if(read_integer(file,"grid_imask",count,grid->mask))goto finish;
    if(boundary==SC_LATITUDE_CIRCLE){for(size_t cell=0;cell<count;++cell)grid->area[cell]=fabs(sc_legacy_area((int)corners,grid->corner_lat+cell*corners,grid->corner_lon+cell*corners));}
    code=sc_grid_prepare(grid);
finish:
    nc_close(file);if(code)sc_grid_free(grid);return code;
}
static int define_variable(int file,const char *name,nc_type type,int rank,const int *dims,int *variable,const char *units) {
    if(nc_def_var(file,name,type,rank,dims,variable))return SC_IO;
    if(units&&nc_put_att_text(file,*variable,"units",strlen(units),units))return SC_IO;
    return SC_OK;
}
int sc_grid_write(const char *path,const sc_grid *grid) {
    if(!path||!grid||!grid->count)return SC_ARGUMENT;
    int file=-1,code=SC_IO,ds,dc,dr;
    if(nc_create(path,NC_CLOBBER,&file))return SC_IO;
    if(nc_def_dim(file,"grid_size",grid->count,&ds)||nc_def_dim(file,"grid_corners",(size_t)grid->corners,&dc)||nc_def_dim(file,"grid_rank",(size_t)grid->rank,&dr))goto finish;
    int center_lat,center_lon,corner_lat,corner_lon,mask,dims,area,axes[2]={ds,dc};
    if(define_variable(file,"grid_center_lat",NC_DOUBLE,1,&ds,&center_lat,"radians")||define_variable(file,"grid_center_lon",NC_DOUBLE,1,&ds,&center_lon,"radians")||define_variable(file,"grid_corner_lat",NC_DOUBLE,2,axes,&corner_lat,"radians")||define_variable(file,"grid_corner_lon",NC_DOUBLE,2,axes,&corner_lon,"radians")||define_variable(file,"grid_imask",NC_INT,1,&ds,&mask,NULL)||define_variable(file,"grid_dims",NC_INT,1,&dr,&dims,NULL)||define_variable(file,"grid_area",NC_DOUBLE,1,&ds,&area,"square radians"))goto finish;
    const char *boundary=grid->boundary==SC_GREAT_CIRCLE?"great_circle":"latitude_circle";
    const char *title="06 SCRIP C generated grid";
    if(nc_put_att_text(file,NC_GLOBAL,"title",strlen(title),title)||nc_put_att_text(file,NC_GLOBAL,"boundary_model",strlen(boundary),boundary)||nc_enddef(file))goto finish;
    if(nc_put_var_double(file,center_lat,grid->lat)||nc_put_var_double(file,center_lon,grid->lon)||nc_put_var_double(file,corner_lat,grid->corner_lat)||nc_put_var_double(file,corner_lon,grid->corner_lon)||nc_put_var_int(file,mask,grid->mask)||nc_put_var_int(file,dims,grid->dims)||nc_put_var_double(file,area,grid->area))goto finish;
    code=SC_OK;
finish:if(nc_close(file)&&!code)code=SC_IO;return code;
}
int sc_map_read(const char *path,sc_map *map) {
    if(!path||!map)return SC_ARGUMENT;
    memset(map,0,sizeof(*map));int file=-1,code=SC_IO;
    if(nc_open(path,NC_NOWRITE,&file))return SC_IO;
    size_t source_count,target_count,links,components;
    if(dimension(file,"src_grid_size",&source_count)||dimension(file,"dst_grid_size",&target_count)||dimension(file,"num_links",&links)||dimension(file,"num_wgts",&components)||(components!=1&&components!=3&&components!=4))goto finish;
    if(links>=SIZE_MAX/(components*sizeof(double))-1)goto finish;
    code=sc_map_alloc(source_count,target_count,(int)components,map);if(code)goto finish;code=SC_IO;
    map->capacity=map->links=links;map->source=malloc((links+1)*sizeof(int32_t));map->target=malloc((links+1)*sizeof(int32_t));map->weight=malloc((links+1)*components*sizeof(double));
    if(!map->source||!map->target||!map->weight){code=SC_MEMORY;goto finish;}
    int variable,rank,dimensions[NC_MAX_VAR_DIMS];
    if(read_integer(file,"src_address",links,map->source)||read_integer(file,"dst_address",links,map->target))goto finish;
    for(size_t link=0;link<links;++link){if(map->source[link]<1||map->target[link]<1||(size_t)map->source[link]>source_count||(size_t)map->target[link]>target_count){code=SC_ARGUMENT;goto finish;}--map->source[link];--map->target[link];}
    if(nc_inq_varid(file,"remap_matrix",&variable)||nc_inq_varndims(file,variable,&rank)||rank!=2||nc_inq_vardimid(file,variable,dimensions))goto finish;
    char first_name[NC_MAX_NAME+1],second_name[NC_MAX_NAME+1];if(nc_inq_dimname(file,dimensions[0],first_name)||nc_inq_dimname(file,dimensions[1],second_name))goto finish;
    if(!strcmp(first_name,"num_links")&&!strcmp(second_name,"num_wgts")){if(nc_get_var_double(file,variable,map->weight))goto finish;}
    else if(!strcmp(first_name,"num_wgts")&&!strcmp(second_name,"num_links")) {
        double *temporary=malloc((links+1)*components*sizeof(double));if(!temporary){code=SC_MEMORY;goto finish;}
        int read_code=nc_get_var_double(file,variable,temporary);
        if(!read_code)for(size_t link=0;link<links;++link)for(size_t c=0;c<components;++c)map->weight[link*components+c]=temporary[c*links+link];
        free(temporary);if(read_code)goto finish;
    } else goto finish;
    for(size_t index=0;index<links*components;++index)if(!isfinite(map->weight[index])){code=SC_ARGUMENT;goto finish;}
    if(read_vector(file,"src_grid_area",source_count,map->source_area)||read_vector(file,"dst_grid_area",target_count,map->target_area)||read_vector(file,"src_grid_frac",source_count,map->source_fraction)||read_vector(file,"dst_grid_frac",target_count,map->target_fraction))goto finish;
    for(size_t cell=0;cell<source_count;++cell)if(!isfinite(map->source_area[cell])||map->source_area[cell]<=0||!isfinite(map->source_fraction[cell])||map->source_fraction[cell]<0){code=SC_ARGUMENT;goto finish;}
    for(size_t cell=0;cell<target_count;++cell)if(!isfinite(map->target_area[cell])||map->target_area[cell]<=0||!isfinite(map->target_fraction[cell])||map->target_fraction[cell]<0){code=SC_ARGUMENT;goto finish;}
    char normalization[64]={0};size_t length;
    if(nc_inq_attlen(file,NC_GLOBAL,"normalization",&length)||length>63||nc_get_att_text(file,NC_GLOBAL,"normalization",normalization))goto finish;
    if(strstr(normalization,"frac"))map->normalization=SC_FRACAREA;else if(strstr(normalization,"dest"))map->normalization=SC_DESTAREA;else if(strstr(normalization,"none"))map->normalization=SC_NONE;else {code=SC_ARGUMENT;goto finish;}
    code=SC_OK;
finish:nc_close(file);if(code)sc_map_free(map);return code;
}
int sc_map_write(const char *path,const sc_map *map,const sc_grid *source,const sc_grid *target) {
    if(!path||!map||!source||!target||source->count!=map->source_count||target->count!=map->target_count)return SC_ARGUMENT;
    int file=-1,code=SC_IO,ds,dt,dl,dw;
    if(nc_create(path,NC_CLOBBER,&file))return SC_IO;
    if(nc_def_dim(file,"src_grid_size",map->source_count,&ds)||nc_def_dim(file,"dst_grid_size",map->target_count,&dt)||nc_def_dim(file,"num_links",map->links,&dl)||nc_def_dim(file,"num_wgts",(size_t)map->components,&dw))goto finish;
    int src_address,dst_address,matrix,axes[2]={dl,dw},variables[2][8];
    if(define_variable(file,"src_address",NC_INT,1,&dl,&src_address,NULL)||define_variable(file,"dst_address",NC_INT,1,&dl,&dst_address,NULL)||define_variable(file,"remap_matrix",NC_DOUBLE,2,axes,&matrix,NULL))goto finish;
    const char *suffix[8]={"grid_area","grid_frac","grid_imask","grid_center_lat","grid_center_lon","grid_dims","grid_corner_lat","grid_corner_lon"};
    for(int side=0;side<2;++side) {
        const sc_grid *grid=side?target:source;char dimension_name[64];int corner_dim,rank_dim;
        snprintf(dimension_name,sizeof(dimension_name),"%s_grid_corners",side?"dst":"src");if(nc_def_dim(file,dimension_name,(size_t)grid->corners,&corner_dim))goto finish;
        snprintf(dimension_name,sizeof(dimension_name),"%s_grid_rank",side?"dst":"src");if(nc_def_dim(file,dimension_name,(size_t)grid->rank,&rank_dim))goto finish;
        for(int item=0;item<8;++item) {
            char name[64];snprintf(name,sizeof(name),"%s_%s",side?"dst":"src",suffix[item]);
            int dimension_id=side?dt:ds,dimensions[2]={dimension_id,corner_dim};
            if(item==5)dimensions[0]=rank_dim;
            if(define_variable(file,name,(item==2||item==5)?NC_INT:NC_DOUBLE,item>=6?2:1,dimensions,&variables[side][item],(item==3||item==4||item>=6)?"radians":NULL))goto finish;
        }
    }
    const char *normalization=map->normalization==SC_FRACAREA?"fracarea":(map->normalization==SC_DESTAREA?"destarea":"none");
    const char *attributes[5][2]={{"title","06 SCRIP C conservative remap"},{"map_method","Conservative remapping"},{"conventions","SCRIP"},{"source_grid","06 input source"},{"dest_grid","06 input destination"}};
    for(int attribute=0;attribute<5;++attribute)if(nc_put_att_text(file,NC_GLOBAL,attributes[attribute][0],strlen(attributes[attribute][1]),attributes[attribute][1]))goto finish;
    if(nc_put_att_text(file,NC_GLOBAL,"normalization",strlen(normalization),normalization)||nc_put_att_text(file,NC_GLOBAL,"implementation",strlen("06_SCRIP_C_Version"),"06_SCRIP_C_Version")||nc_enddef(file))goto finish;
    int32_t *address=malloc((map->links+1)*sizeof(int32_t));if(!address){code=SC_MEMORY;goto finish;}
    for(size_t link=0;link<map->links;++link)address[link]=map->source[link]+1;
    int result=nc_put_var_int(file,src_address,address);
    for(size_t link=0;link<map->links;++link)address[link]=map->target[link]+1;
    result|=nc_put_var_int(file,dst_address,address);free(address);
    if(result||nc_put_var_double(file,matrix,map->weight))goto finish;
    for(int side=0;side<2;++side){const sc_grid *grid=side?target:source;const double *area=side?map->target_area:map->source_area,*fraction=side?map->target_fraction:map->source_fraction;
        if(nc_put_var_double(file,variables[side][0],area)||nc_put_var_double(file,variables[side][1],fraction)||nc_put_var_int(file,variables[side][2],grid->mask)||nc_put_var_double(file,variables[side][3],grid->lat)||nc_put_var_double(file,variables[side][4],grid->lon)||nc_put_var_int(file,variables[side][5],grid->dims)||nc_put_var_double(file,variables[side][6],grid->corner_lat)||nc_put_var_double(file,variables[side][7],grid->corner_lon))goto finish;}
    code=SC_OK;
finish:if(nc_close(file)&&!code)code=SC_IO;return code;
}

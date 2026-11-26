/* 2026-11-21: C原生二十面体及共享边中点细分；无外部几何库。 */
#include "26NOV21_scrip.h"
#include <stdlib.h>
#include <string.h>
typedef struct { int32_t first,second,face,edge; } edge_entry;
static int compare_edge(const void *left,const void *right) {
    const edge_entry *a=left,*b=right;
    if(a->first!=b->first) return a->first<b->first?-1:1;
    return (a->second>b->second)-(a->second<b->second);
}
void sc_mesh_free(sc_mesh *mesh) { if(mesh) { free(mesh->xyz);free(mesh->faces);memset(mesh,0,sizeof(*mesh)); } }
int sc_icosphere(int level,sc_mesh *mesh) {
    if(!mesh||level<0||level>7) return SC_ARGUMENT;
    memset(mesh,0,sizeof(*mesh));
    double golden=1.6180339887498948482;
    sc_vec initial[12]={{-1,golden,0},{1,golden,0},{-1,-golden,0},{1,-golden,0},{0,-1,golden},{0,1,golden},{0,-1,-golden},{0,1,-golden},{golden,0,-1},{golden,0,1},{-golden,0,-1},{-golden,0,1}};
    int32_t faces[60]={0,11,5,0,5,1,0,1,7,0,7,10,0,10,11,1,5,9,5,11,4,11,10,2,10,7,6,7,1,8,3,9,4,3,4,2,3,2,6,3,6,8,3,8,9,4,9,5,2,4,11,6,2,10,8,6,7,9,8,1};
    mesh->xyz=malloc(12*sizeof(sc_vec));mesh->faces=malloc(sizeof(faces));
    if(!mesh->xyz||!mesh->faces) {sc_mesh_free(mesh);return SC_MEMORY;}
    mesh->vertex_count=12;mesh->face_count=20;
    for(int index=0;index<12;++index) mesh->xyz[index]=sc_unit(initial[index]);
    memcpy(mesh->faces,faces,sizeof(faces));
    for(int step=0;step<level;++step) {
        size_t edges=3*mesh->face_count;
        edge_entry *entries=malloc(edges*sizeof(*entries));
        int32_t *midpoint=malloc(edges*sizeof(*midpoint)),*new_faces=malloc(4*edges*sizeof(*new_faces));
        sc_vec *new_xyz=realloc(mesh->xyz,(mesh->vertex_count+edges/2)*sizeof(sc_vec));
        if(new_xyz)mesh->xyz=new_xyz;
        if(!entries||!midpoint||!new_faces||!new_xyz) {free(entries);free(midpoint);free(new_faces);sc_mesh_free(mesh);return SC_MEMORY;}
        for(size_t face=0;face<mesh->face_count;++face) for(int edge=0;edge<3;++edge) {
            int32_t a=mesh->faces[3*face+(size_t)edge],b=mesh->faces[3*face+(size_t)((edge+1)%3)];
            entries[3*face+(size_t)edge]=(edge_entry){a<b?a:b,a<b?b:a,(int32_t)face,edge};
        }
        qsort(entries,edges,sizeof(*entries),compare_edge);
        for(size_t index=0;index<edges;index+=2) {
            if(index+1>=edges||entries[index].first!=entries[index+1].first||entries[index].second!=entries[index+1].second) {free(entries);free(midpoint);free(new_faces);sc_mesh_free(mesh);return SC_GEOMETRY;}
            int32_t vertex=(int32_t)mesh->vertex_count++;
            mesh->xyz[vertex]=sc_unit(sc_add(mesh->xyz[entries[index].first],mesh->xyz[entries[index].second]));
            for(size_t side=0;side<2;++side) midpoint[3*(size_t)entries[index+side].face+(size_t)entries[index+side].edge]=vertex;
        }
        for(size_t face=0;face<mesh->face_count;++face) {
            int32_t a=mesh->faces[3*face],b=mesh->faces[3*face+1],c=mesh->faces[3*face+2];
            int32_t ab=midpoint[3*face],bc=midpoint[3*face+1],ca=midpoint[3*face+2];
            int32_t four[12]={a,ab,ca,b,bc,ab,c,ca,bc,ab,bc,ca};
            memcpy(new_faces+12*face,four,sizeof(four));
        }
        free(entries);free(midpoint);free(mesh->faces);mesh->faces=new_faces;mesh->face_count*=4;
    }
    return SC_OK;
}

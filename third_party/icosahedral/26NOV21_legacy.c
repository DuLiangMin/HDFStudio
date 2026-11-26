/* SCRIP兼容公式的C派生，2026-11-21。
 * 来源: a5d6960ac962, remap_conservative.f:3233-3381 / grids.f:1199-1286。
 * Copyright (c) 1997, 1998 the Regents of the University of California.
 * 完整原始Notice保存在 ../LICENSE_SCRIP；本文件为明确标记的修改版。 */
#include "26NOV21_scrip.h"
#include <math.h>
/* 兼容层故意保留04 constants.f的截断pi；新大圆几何使用全精度SC_PI。 */
static const double legacy_pi=3.14159265359;
static double wrap_once(double angle) { if(angle>legacy_pi) angle-=2*legacy_pi; else if(angle< -legacy_pi) angle+=2*legacy_pi; return angle; }
void sc_line_integral_phi(double phi1,double phi2,double theta1,double theta2,double longitude1,double longitude2,double weight[6]) {
    double sin1=sin(theta1),sin2=sin(theta2),cos1=cos(theta1),cos2=cos(theta2);
    double dphi=0.5*wrap_once(phi1-phi2);
    weight[0]=weight[3]=dphi*(sin1+sin2);
    weight[1]=weight[4]=dphi*(cos1+cos2+(theta1*sin1+theta2*sin2));
    double f1=0.5*(cos1*sin1+theta1),f2=0.5*(cos2*sin2+theta2);
    double references[2]={longitude1,longitude2};
    for(int grid=0;grid<2;++grid) {
        double first=wrap_once(phi1-references[grid]),second=wrap_once(phi2-references[grid]);
        if(second-first<legacy_pi && second-first> -legacy_pi) weight[grid*3+2]=dphi*(first*f1+second*f2);
        else {
            double factor=first>0?legacy_pi:-legacy_pi;
            double integral=f1+(f2-f1)*(factor-first)/fabs(dphi);
            weight[grid*3+2]=0.5*first*(first-factor)*f1-0.5*second*(second+factor)*f2+0.5*factor*(first+second)*integral;
        }
    }
}
double sc_legacy_area(int corners,const double *latitude,const double *longitude) {
    double area=0; for(int corner=0;corner<corners;++corner) { int next=(corner+1)%corners;
        area+=0.5*wrap_once(longitude[corner]-longitude[next])*(sin(latitude[corner])+sin(latitude[next])); }
    return area;
}
void sc_phi_batch(int count,const double *coordinates,double *weights,int repeats) {
    for(int repeat=0;repeat<repeats;++repeat)for(int cell=0;cell<count;++cell){const double *c=coordinates+6*(size_t)cell;sc_line_integral_phi(c[0],c[1],c[2],c[3],c[4],c[5],weights+6*(size_t)cell);}
}
void sc_area_batch(int count,int corners,const double *latitude,const double *longitude,double *areas,int repeats) {
    for(int repeat=0;repeat<repeats;++repeat)for(int cell=0;cell<count;++cell)areas[cell]=sc_legacy_area(corners,latitude+(size_t)cell*(size_t)corners,longitude+(size_t)cell*(size_t)corners);
}

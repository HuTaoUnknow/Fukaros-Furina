#include "pointer_gain.h"
#include <math.h>
int fr_pointer_gain(fr_pointer_fraction *f,int percent,long l,long t,long r,long b,long x,long y,long nx,long ny,long *ox,long *oy){
 *ox=nx;*oy=ny;
 if(percent<25||percent>400||percent==100||x<l||x>=r||y<t||y>=b||nx<l||nx>=r||ny<t||ny>=b){f->x=f->y=0;return 0;}
 double dx=(double)(nx-x)*percent/100+f->x,dy=(double)(ny-y)*percent/100+f->y;
 long ix=(long)trunc(dx),iy=(long)trunc(dy);f->x=dx-ix;f->y=dy-iy;*ox=x+ix;*oy=y+iy;
 return *ox!=nx||*oy!=ny;
}

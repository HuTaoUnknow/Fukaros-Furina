#include "pointer_gain.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Pointer gain failed: line %d\n",__LINE__);return 1;}}while(0)
int main(void){
 fr_pointer_fraction f={0};long x,y;
 CHECK(!fr_pointer_gain(&f,200,1000,0,2200,1920,100,100,110,110,&x,&y)&&x==110&&y==110);
 CHECK(fr_pointer_gain(&f,200,1000,0,2200,1920,1100,100,1110,105,&x,&y)&&x==1120&&y==110);
 CHECK(!fr_pointer_gain(&f,200,1000,0,2200,1920,1001,100,999,100,&x,&y)&&x==999);
 CHECK(!fr_pointer_gain(&f,200,1000,0,2200,1920,999,100,1001,100,&x,&y)&&x==1001);
 long pos=1100;f=(fr_pointer_fraction){0};
 for(int i=0;i<4;++i){fr_pointer_gain(&f,25,1000,0,2200,1920,pos,100,pos+1,100,&x,&y);pos=x;}
 CHECK(pos==1101);
 CHECK(fr_pointer_gain(&f,200,-1200,-1920,0,0,-500,-500,-490,-495,&x,&y)&&x==-480&&y==-490);
 CHECK(!fr_pointer_gain(&f,100,1000,0,2200,1920,1100,100,1110,110,&x,&y)&&x==1110);
 puts("Pointer gain: primary unchanged, crossings preserved, fractional and negative-coordinate movement passed.");return 0;
}

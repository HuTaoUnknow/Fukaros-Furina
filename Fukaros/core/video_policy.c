#include "video_policy.h"
#include "furina_protocol.h"
#include <string.h>
int fr_resolution_valid(uint32_t edge){return edge==0||edge==720||edge==1080||edge==1920;}
int fr_video_bitrate(int width,int height){
 int64_t rate=(int64_t)width*height*10;return (int)(rate<6000000?6000000:(rate>28000000?28000000:rate));
}
void fr_video_policy_init(fr_video_policy *p,int selected){memset(p,0,sizeof(*p));p->selected=fr_resolution_valid(selected)?selected:0;p->edge=p->selected?p->selected:1080;p->ceiling=1920;p->cooldown=3;}
int fr_video_policy_sample(fr_video_policy *p,double fps,double work_ms,unsigned queue_ms){
 if(p->selected)return 0;
 // Ignore decoder warm-up and short layout/key-frame disturbances. Once a rung
 // proves unsustainable, don't probe it repeatedly in the same auto session.
 if(p->cooldown>0){--p->cooldown;p->bad=p->good=p->severe=0;return 0;}
 const double ratio=p->edge==720?1.5:(1920.0/1080.0);
 const int headroom=work_ms*ratio*ratio<=14.0;
 if(fps<45.0||work_ms>25.0||queue_ms>120)++p->severe;else p->severe=0;
 if(fps<59.0||work_ms>16.0||queue_ms>50){++p->bad;p->good=0;}else if(fps>=59.5&&work_ms<10.0&&headroom&&queue_ms<20){++p->good;p->bad=p->severe=0;}else{p->bad=p->good=0;}
 if((p->bad>=5||p->severe>=2)&&p->edge>720){p->edge=p->edge==1920?1080:720;p->ceiling=p->edge;p->bad=p->good=p->severe=0;p->cooldown=15;return 1;}
 if(p->good>=30&&p->edge<p->ceiling){p->edge=p->edge==720?1080:1920;p->bad=p->good=p->severe=0;p->cooldown=10;return 1;}
 return 0;
}
void fr_video_dimensions(int w,int h,int edge,int *out_w,int *out_h){
 if(w<2||h<2){*out_w=*out_h=0;return;}
 const int max=w>h?w:h;double scale=max>edge?(double)edge/max:1.0;
 *out_w=((int)(w*scale)/2)*2;*out_h=((int)(h*scale)/2)*2;if(*out_w<2)*out_w=2;if(*out_h<2)*out_h=2;
}
int fr_video_write(uint8_t *p,const fr_video_info *i){
 if(!p||!i||i->flags>FR_VIDEO_KEY||i->width<2||i->height<2||i->width>4096||i->height>4096||(i->width&1)||(i->height&1))return 0;
 fr_put_u32(p,i->flags);fr_put_u32(p+4,i->sequence);fr_put_u32(p+8,i->width);fr_put_u32(p+12,i->height);
 fr_put_u32(p+16,(uint32_t)(i->pts_us>>32));fr_put_u32(p+20,(uint32_t)i->pts_us);fr_put_u32(p+24,i->epoch);fr_put_u32(p+28,1);return 1;
}
int fr_video_read(const uint8_t *p,size_t n,fr_video_info *i){
 if(!p||!i||n<=FR_VIDEO_HEADER||n>FR_MAX_PAYLOAD||fr_get_u32(p+28)!=1)return 0;
 i->flags=fr_get_u32(p);i->sequence=fr_get_u32(p+4);i->width=fr_get_u32(p+8);i->height=fr_get_u32(p+12);i->pts_us=((uint64_t)fr_get_u32(p+16)<<32)|fr_get_u32(p+20);i->epoch=fr_get_u32(p+24);
 return i->flags<=FR_VIDEO_KEY&&i->width>=2&&i->height>=2&&i->width<=4096&&i->height<=4096&&!(i->width&1)&&!(i->height&1);
}

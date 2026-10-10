#include "video_policy.h"
#include "furina_protocol.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 fr_video_policy p;fr_video_policy_init(&p,0);CHECK(p.edge==1080&&p.selected==0);
 /* Startup, a two-second dip and isolated key-frame spikes don't change size. */
 for(int i=0;i<3;i++)CHECK(!fr_video_policy_sample(&p,0,40,180));CHECK(p.edge==1080);
 for(int i=0;i<2;i++)CHECK(!fr_video_policy_sample(&p,58.5,2,0));CHECK(p.edge==1080);
 CHECK(!fr_video_policy_sample(&p,60,2,0));
 for(int i=0;i<30;i++){CHECK(!fr_video_policy_sample(&p,i%4==0?57.0:60.0,5,0));}CHECK(p.edge==1080);
 /* Sustained load lowers size; stay there rather than repeatedly probing up. */
 for(int i=0;i<4;i++)CHECK(!fr_video_policy_sample(&p,58.5,2,0));CHECK(fr_video_policy_sample(&p,58.5,2,0));CHECK(p.edge==720);
 for(int i=0;i<120;i++)CHECK(!fr_video_policy_sample(&p,60,2,0));CHECK(p.edge==720);
 fr_video_policy_init(&p,1920);for(int i=0;i<30;i++)CHECK(!fr_video_policy_sample(&p,20,40,100));CHECK(p.edge==1920);
 fr_video_policy_init(&p,999);CHECK(p.selected==0&&p.edge==1080);
 fr_video_policy_init(&p,0);for(int i=0;i<3;i++)fr_video_policy_sample(&p,60,2,0);
 CHECK(!fr_video_policy_sample(&p,42,28,150));CHECK(fr_video_policy_sample(&p,42,28,150));CHECK(p.edge==720);
 fr_video_policy_init(&p,0);for(int i=0;i<20;i++)CHECK(!fr_video_policy_sample(&p,60,5.0,0));CHECK(p.edge==1080);
 for(int i=0;i<29;i++)CHECK(!fr_video_policy_sample(&p,60,2.0,0));CHECK(fr_video_policy_sample(&p,60,2.0,0));CHECK(p.edge==1920);
 for(int i=0;i<10;i++)CHECK(!fr_video_policy_sample(&p,60,2,0));
 for(int i=0;i<4;i++)CHECK(!fr_video_policy_sample(&p,60,2,80));CHECK(fr_video_policy_sample(&p,60,2,80));CHECK(p.edge==1080);
 for(int i=0;i<120;i++)CHECK(!fr_video_policy_sample(&p,60,2,0));CHECK(p.edge==1080);
 CHECK(fr_video_bitrate(1920,1200)==23040000);CHECK(fr_video_bitrate(1080,674)==7279200);CHECK(fr_video_bitrate(720,450)==6000000);CHECK(fr_video_bitrate(4096,2160)==28000000);CHECK(FR_VIDEO_GOP==300);
 int w,h;fr_video_dimensions(2560,1600,1920,&w,&h);CHECK(w==1920&&h==1200);fr_video_dimensions(1800,2880,1080,&w,&h);CHECK(w==674&&h==1080);fr_video_dimensions(640,480,720,&w,&h);CHECK(w==640&&h==480);
 uint8_t data[33]={0};fr_video_info in={FR_VIDEO_KEY,123,1920,1200,8,0x123456789ABCULL},out;
 CHECK(fr_video_write(data,&in));data[32]=1;CHECK(fr_video_read(data,sizeof data,&out));CHECK(in.flags==out.flags&&in.sequence==out.sequence&&in.width==out.width&&in.height==out.height&&in.epoch==out.epoch&&in.pts_us==out.pts_us);
 CHECK(!fr_video_read(data,32,&out));data[31]=2;CHECK(!fr_video_read(data,sizeof data,&out));data[31]=1;fr_put_u32(data+8,1921);CHECK(!fr_video_read(data,sizeof data,&out));in.width=1921;CHECK(!fr_video_write(data,&in));
 puts("60 FPS policy, congestion hysteresis, resolution and H.264 envelope passed");return 0;
}

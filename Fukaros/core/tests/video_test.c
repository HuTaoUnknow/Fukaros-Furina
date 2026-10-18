#include "video_policy.h"
#include "furina_protocol.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 fr_video_policy p;fr_video_policy_init(&p,0);CHECK(p.edge==1080&&p.selected==0);
 /* Stable measured 60 FPS probes progressively up to the highest rung. */
 for(int i=0;i<4;i++)CHECK(!fr_video_policy_sample(&p,60,2,0));CHECK(p.edge==1080);
 CHECK(fr_video_policy_sample(&p,60,2,0));CHECK(p.edge==1440);
 for(int i=0;i<4;i++)CHECK(!fr_video_policy_sample(&p,60,2,0));CHECK(fr_video_policy_sample(&p,60,2,0));CHECK(p.edge==1920);
 for(int i=0;i<20;i++)CHECK(!fr_video_policy_sample(&p,60,2,0));CHECK(p.edge==1920);
 /* 50 FPS, slow work and backlog do not trigger a quality drop. */
 for(int i=0;i<30;i++)CHECK(!fr_video_policy_sample(&p,50,40,500));CHECK(p.edge==1920);
 for(int i=0;i<4;i++)CHECK(!fr_video_policy_sample(&p,49,40,500));CHECK(p.edge==1920);
 CHECK(!fr_video_policy_sample(&p,50,40,500));
 CHECK(!fr_video_policy_sample_elapsed(&p,49.9,40,500,4900));CHECK(p.edge==1920);
 CHECK(!fr_video_policy_sample_elapsed(&p,49.9,40,500,99));CHECK(p.edge==1920);
 CHECK(fr_video_policy_sample_elapsed(&p,49.9,40,500,1));CHECK(p.edge==1440);
 /* Brief dips reset at >=50; a full five seconds lowers one rung. */
 for(int i=0;i<4;i++)CHECK(!fr_video_policy_sample(&p,10,2,0));CHECK(!fr_video_policy_sample(&p,60,2,0));CHECK(p.edge==1440);
 for(int i=0;i<4;i++)CHECK(!fr_video_policy_sample(&p,10,2,0));CHECK(fr_video_policy_sample(&p,10,2,0));CHECK(p.edge==1080);
 for(int i=0;i<4;i++)CHECK(!fr_video_policy_sample(&p,0,40,500));CHECK(fr_video_policy_sample(&p,0,40,500));CHECK(p.edge==720);
 for(int i=0;i<30;i++)CHECK(!fr_video_policy_sample(&p,0,40,500));CHECK(p.edge==720);
 /* Recovery probes again; a prior drop does not impose a permanent ceiling. */
 CHECK(fr_video_policy_sample_elapsed(&p,60,2,0,5000));CHECK(p.edge==1080);
 CHECK(fr_video_policy_sample_elapsed(&p,60,2,0,5000));CHECK(p.edge==1440);
 CHECK(fr_video_policy_sample_elapsed(&p,60,2,0,5000));CHECK(p.edge==1920);
 /* Uneven sampling intervals count elapsed time, not the number of callbacks. */
 fr_video_policy_init(&p,0);CHECK(!fr_video_policy_sample_elapsed(&p,40,2,0,1600));CHECK(!fr_video_policy_sample_elapsed(&p,40,2,0,2100));CHECK(!fr_video_policy_sample_elapsed(&p,40,2,0,1299));CHECK(p.edge==1080);CHECK(fr_video_policy_sample_elapsed(&p,40,2,0,1));CHECK(p.edge==720);
 for(int i=0;i<9;i++)CHECK(!fr_video_policy_sample(&p,60,2,0));CHECK(fr_video_policy_sample(&p,60,2,0));CHECK(p.edge==1080);
 fr_video_policy_init(&p,1920);for(int i=0;i<30;i++)CHECK(!fr_video_policy_sample(&p,20,40,100));CHECK(p.edge==1920);
 fr_video_policy_init(&p,999);CHECK(p.selected==0&&p.edge==1080);CHECK(!fr_video_policy_sample_elapsed(&p,0,40,500,0));
 CHECK(fr_video_bitrate(1920,1200)==23040000);CHECK(fr_video_bitrate(1080,674)==7279200);CHECK(fr_video_bitrate(720,450)==6000000);CHECK(fr_video_bitrate(4096,2160)==28000000);CHECK(FR_VIDEO_GOP==300);
 int w,h;fr_video_dimensions(2560,1600,1920,&w,&h);CHECK(w==1920&&h==1200);fr_video_dimensions(1800,2880,1080,&w,&h);CHECK(w==674&&h==1080);fr_video_dimensions(640,480,720,&w,&h);CHECK(w==640&&h==480);
 uint8_t data[33]={0};fr_video_info in={FR_VIDEO_KEY,123,1920,1200,8,0x123456789ABCULL},out;
 CHECK(fr_video_write(data,&in));data[32]=1;CHECK(fr_video_read(data,sizeof data,&out));CHECK(in.flags==out.flags&&in.sequence==out.sequence&&in.width==out.width&&in.height==out.height&&in.epoch==out.epoch&&in.pts_us==out.pts_us);
 CHECK(!fr_video_read(data,32,&out));data[31]=2;CHECK(!fr_video_read(data,sizeof data,&out));data[31]=1;fr_put_u32(data+8,1921);CHECK(!fr_video_read(data,sizeof data,&out));in.width=1921;CHECK(!fr_video_write(data,&in));
 in.width=1200;in.flags=FR_VIDEO_KEY|FR_VIDEO_CURSOR_SEPARATE;CHECK(fr_video_write(data,&in));CHECK(fr_video_read(data,sizeof data,&out));CHECK(out.flags==3);in.flags=4;CHECK(!fr_video_write(data,&in));
 puts("Progressive auto resolution, five-second frame-loss threshold, timing and H.264 envelope passed");return 0;
}

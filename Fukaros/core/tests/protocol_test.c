#include "furina_protocol.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 uint8_t h[12],p[20];fr_header decoded;fr_gesture g={0,65535,321,654,800},got;uint32_t x,y;
 CHECK(fr_encode_header(h,FR_FRAME,FR_MAX_PAYLOAD));CHECK(fr_decode_header(h,&decoded));CHECK(decoded.length==FR_MAX_PAYLOAD&&decoded.type==FR_FRAME);
 CHECK(!fr_encode_header(h,FR_FRAME,FR_MAX_PAYLOAD+1));CHECK(!fr_encode_header(h,0,0));
 h[4]=2;CHECK(!fr_decode_header(h,&decoded));h[4]=1;h[6]=1;CHECK(!fr_decode_header(h,&decoded));h[6]=0;h[0]='X';CHECK(!fr_decode_header(h,&decoded));
 CHECK(fr_encode_gesture(p,&g));CHECK(fr_decode_gesture(p,20,&got));CHECK(memcmp(&g,&got,sizeof g)==0);CHECK(!fr_decode_gesture(p,19,&got));
 fr_put_u32(p,65536);CHECK(!fr_decode_gesture(p,20,&got));g.duration_ms=0;CHECK(!fr_encode_gesture(p,&g));
 CHECK(fr_map_point(500,250,1000,500,100,100,&x,&y));CHECK(x==32768&&y==32768);
 CHECK(!fr_map_point(200,250,1000,500,100,100,&x,&y));CHECK(!fr_map_point(750,250,1000,500,100,100,&x,&y));
 CHECK(!fr_map_point(0,0,0,500,100,100,&x,&y));
 CHECK(fr_encode_header(h,FR_SESSION,4));CHECK(fr_decode_header(h,&decoded));CHECK(decoded.type==FR_SESSION);
 CHECK(fr_valid_session_action(FR_START_SHARE));CHECK(fr_valid_session_action(FR_RESTORE));
 CHECK(fr_valid_session_action(FR_REQUEST_KEY));CHECK(fr_valid_session_action(FR_START_RECEIVE));CHECK(fr_valid_session_action(FR_STOP_RECEIVE));
 CHECK(!fr_valid_session_action(0));CHECK(!fr_valid_session_action(10));
 puts("Protocol, session requests and letterbox mapping passed");return 0;
}

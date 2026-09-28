// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/core/cdc_control.h"
#include "../src/core/spsc.h"
#include "../src/core/bridge.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
static cdc_reply_t request(cdc_control_t *s,uint8_t type,uint8_t req,uint16_t v,uint16_t i,uint16_t n) {
 uint8_t q[]={type,req,(uint8_t)v,(uint8_t)(v>>8),(uint8_t)i,(uint8_t)(i>>8),(uint8_t)n,(uint8_t)(n>>8)};
 return cdc_control_setup(s,q);
}
static void descriptors(void) {
 cdc_control_t s;const uint8_t serial[]={0,1,2,3,0xab,0xcd,0xef,0xff};cdc_control_init(&s,serial);
 assert(cdc_device_descriptor[0]==18 && cdc_device_descriptor[7]==64);
 assert(cdc_device_descriptor[10]!=1); // distinct from HID PID 0001
 unsigned len=cdc_config_descriptor[2]|(unsigned)cdc_config_descriptor[3]<<8;
 assert(len==CDC_CONFIG_LEN);
 unsigned pos=0,itfs=0,eps=0,iads=0;
 while(pos<len) {
  const uint8_t *p=cdc_config_descriptor+pos;assert(p[0]>=2 && pos+p[0]<=len);
  if(p[1]==4){assert(p[2]==itfs);itfs++;}
  if(p[1]==11){assert(p[2]==0 && p[3]==2);iads++;}
  if(p[1]==5){assert(p[0]==7);assert(p[2]==0x81 || p[2]==2 || p[2]==0x82);assert(p[4]==(p[2]==0x81?16:64));eps++;}
  pos+=p[0];
 }
 assert(itfs==2 && eps==3 && iads==1);
 for(unsigned n=0;n<256;n++) {
  cdc_reply_t r=request(&s,0x80,6,0x200,0,(uint16_t)n);
  assert(r.action==CDC_IN && r.len==(n<len?n:len));assert(!memcmp(r.data,cdc_config_descriptor,r.len));
 }
 for(unsigned idx=0;idx<256;idx++) {
  cdc_reply_t r=request(&s,0x80,6,(uint16_t)(0x300+idx),idx?0x409:0,255);
  if(idx<=4) {assert(r.action==CDC_IN && r.len==r.data[0]);assert(r.data[1]==3 && !(r.len&1));}
  else assert(r.action==CDC_STALL);
 }
 cdc_reply_t r=request(&s,0x80,6,0x303,0x409,255);
 assert(r.len==34 && r.data[18]=='A' && r.data[20]=='B');
 assert(request(&s,0x80,6,0x600,0,10).action==CDC_STALL); // FS only, no qualifier
}
static void requests(void) {
 cdc_control_t s;uint8_t id[8]={0};cdc_control_init(&s,id);
 assert(request(&s,0x21,0x22,1,0,0).action==CDC_STALL);
 assert(request(&s,0,5,127,0,0).address==127);
 assert(request(&s,0,5,128,0,0).action==CDC_STALL);
 assert(request(&s,0,9,2,0,0).action==CDC_STALL);
 assert(request(&s,0,9,1,0,0).configuration==1);s.configuration=1;
 assert(request(&s,0,5,1,0,0).action==CDC_STALL);
 assert(request(&s,0x80,8,0,0,1).data[0]==1);
 assert(request(&s,0x21,0x22,3,0,0).action==CDC_STATUS && s.control_lines==3);
 assert(request(&s,0x21,0x22,4,0,0).action==CDC_STALL);
 assert(request(&s,0x21,0x22,1,1,0).action==CDC_STALL);
 assert(request(&s,0x21,0x20,0,0,7).action==CDC_LINE_OUT);
 assert(request(&s,0x21,0x20,0,0,8).action==CDC_STALL);
 uint8_t line[]={0,0xc2,1,0,0,0,8};assert(cdc_control_line(&s,line,7));
 assert(!memcmp(request(&s,0xa1,0x21,0,0,7).data,line,7));
 assert(!cdc_control_line(&s,line,6));line[6]=9;assert(!cdc_control_line(&s,line,7));
 for(unsigned a=0;a<256;a++) {
  bool valid=a==0x81 || a==2 || a==0x82;
  assert((request(&s,2,3,0,(uint16_t)a,0).action==CDC_STATUS)==valid);
 }
 s.halt[2]=true;assert(request(&s,0x82,0,0,0x82,2).data[0]==1);
 assert(request(&s,0x80,0,0,0,2).data[0]==1);
 assert(request(&s,0x81,10,0,1,1).action==CDC_IN);
 assert(request(&s,1,11,1,1,0).action==CDC_STALL);
 assert(request(&s,0x21,0x22,0,0,0).action==CDC_STATUS && s.control_lines==0);
 cdc_control_reset(&s);assert(!s.configuration && !s.control_lines && !s.halt[2]);
 // Arbitrary setup requests must stay bounded and never return an uninitialized buffer.
 uint32_t rng=7;for(unsigned j=0;j<100000;j++) {
  uint8_t q[8];for(unsigned k=0;k<8;k++){rng=rng*1664525u+1013904223u;q[k]=(uint8_t)(rng>>24);}
  cdc_reply_t r=cdc_control_setup(&s,q);assert(r.len<=sizeof(r.data));
 }
}
static spsc_t ring;
static void *producer(void *arg) {
 (void)arg;
 for(unsigned i=0;i<1000000;) {uint8_t b[37];unsigned n=1000000-i;if(n>37)n=37;for(unsigned k=0;k<n;k++)b[k]=(uint8_t)(i+k);
 if(spsc_write(&ring,b,n))i+=n;else sched_yield();}
 return NULL;
}
static void rings(void) {
 uint8_t a[SPSC_SIZE],b[SPSC_SIZE];memset(a,0x5a,sizeof(a));
 atomic_store(&ring.head,UINT32_MAX-100);atomic_store(&ring.tail,UINT32_MAX-100);
 assert(spsc_write(&ring,a,sizeof(a)));assert(!spsc_write(&ring,a,1));assert(!spsc_free(&ring));
 assert(spsc_read(&ring,b,sizeof(b))==sizeof(b));assert(!memcmp(a,b,sizeof(a)));assert(!spsc_count(&ring));
 pthread_t t;assert(!pthread_create(&t,NULL,producer,NULL));
 for(unsigned i=0;i<1000000;) {unsigned n=spsc_read(&ring,b,53);for(unsigned k=0;k<n;k++)assert(b[k]==(uint8_t)(i+k));i+=n;}
 assert(!pthread_join(t,NULL));assert(!spsc_count(&ring));
}
static uint8_t reply_ch;
static void send_reply(void *ctx,uint8_t ch,const uint8_t *p,size_t n){(void)ctx;(void)p;(void)n;reply_ch=ch;}
static void frame(bridge_t *b,uint8_t ch,uint8_t type,const uint8_t *p,uint8_t n,uint32_t time) {
 uint8_t out[PROTO_MAX_FRAME];size_t len=proto_encode(out,type,0,p,n);
 for(size_t i=0;i<len;i++)bridge_rx_byte(b,ch,out[i],time);
}
static void channels(void) {
 bridge_t b;bridge_io_t io={.send=send_reply};uint8_t id[8]={0},v=1,key[7]={0,4};
 bridge_init(&b,&io,id);bridge_set_usb_state(&b,true,false);
 frame(&b,0,PROTO_T_HELLO,&v,1,0);frame(&b,0,PROTO_T_KEYBOARD,key,7,1);bridge_commit_report(&b,BRIDGE_IF_KEYBOARD);
 frame(&b,1,PROTO_T_HELLO,&v,1,2);assert(b.active_ch==1 && reply_ch==1);
 uint8_t report[8],n;assert(bridge_peek_report(&b,BRIDGE_IF_KEYBOARD,report,&n));for(unsigned i=0;i<n;i++)assert(!report[i]);bridge_commit_report(&b,BRIDGE_IF_KEYBOARD);
 frame(&b,1,PROTO_T_KEYBOARD,key,7,3);bridge_commit_report(&b,BRIDGE_IF_KEYBOARD);
 bridge_channel_lost(&b,0);assert(b.active_ch==1);
 bridge_channel_lost(&b,1);assert(b.active_ch==BRIDGE_CH_NONE);
 frame(&b,1,PROTO_T_PING,NULL,0,490);assert(b.last_valid_ms==3);
 bridge_tick(&b,504);assert(b.wd_fired);assert(bridge_peek_report(&b,BRIDGE_IF_KEYBOARD,report,&n));for(unsigned i=0;i<n;i++)assert(!report[i]);
 frame(&b,0,PROTO_T_HELLO,&v,1,505);assert(b.active_ch==0 && !b.wd_fired && reply_ch==0);
}
int main(void){descriptors();requests();rings();channels();puts("CDC: descriptors, requests + 100000 fuzz, SPSC 1000000 bytes, channel loss/switch: PASS");}

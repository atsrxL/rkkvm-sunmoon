// SPDX-License-Identifier: GPL-3.0-or-later
#include "cdc_control.h"
#include <string.h>
#ifndef RKMOON_USB_VID
#define RKMOON_USB_VID 0x1209
#endif
#ifndef RKMOON_CDC_PID
#define RKMOON_CDC_PID 0x0002
#endif
const uint8_t cdc_device_descriptor[18] = {
 18,1,0,2,0xef,2,1,64,RKMOON_USB_VID&255,RKMOON_USB_VID>>8,
 RKMOON_CDC_PID&255,RKMOON_CDC_PID>>8,0,2,1,2,3,1
};
const uint8_t cdc_config_descriptor[CDC_CONFIG_LEN] = {
 9,2,75,0,2,1,0,0xc0,0, // self powered by native port, no remote wakeup
 8,11,0,2,2,2,1,4, // IAD
 9,4,0,0,1,2,2,1,4,
 5,0x24,0,0x10,1, // CDC 1.10
 5,0x24,1,0,1, // call management, data interface 1
 4,0x24,2,2, // ACM: line coding/control line/serial state
 5,0x24,6,0,1, // union
 7,5,CDC_EP_NOTIFY,3,16,0,16,
 9,4,1,0,2,10,0,0,4,
 7,5,CDC_EP_OUT,2,64,0,0,
 7,5,CDC_EP_IN,2,64,0,0
};
static uint16_t rd(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static int ep_index(uint16_t e) { return e==CDC_EP_NOTIFY?0:e==CDC_EP_OUT?1:e==CDC_EP_IN?2:-1; }
void cdc_control_reset(cdc_control_t *s) {
 s->configuration=0; s->control_lines=0; memset(s->halt,0,sizeof(s->halt));
 const uint8_t line[]={0x40,0x42,0x0f,0,0,0,8}; memcpy(s->line,line,7);
}
void cdc_control_init(cdc_control_t *s,const uint8_t serial[8]) {
 memset(s,0,sizeof(*s)); cdc_control_reset(s); s->serial[0]=34;s->serial[1]=3;
 const char *hex="0123456789ABCDEF";
 for(unsigned i=0;i<8;i++){s->serial[2+i*4]=(uint8_t)hex[serial[i]>>4];s->serial[4+i*4]=(uint8_t)hex[serial[i]&15];}
}
bool cdc_control_line(cdc_control_t *s,const uint8_t *data,unsigned len) {
 if(len!=7 || data[4]>2 || data[5]>4 || (data[6]!=5 && data[6]!=6 && data[6]!=7 && data[6]!=8 && data[6]!=16)) return false;
 memcpy(s->line,data,7);return true; // baud is deliberately ignored
}
cdc_reply_t cdc_control_setup(cdc_control_t *s,const uint8_t q[8]) {
 cdc_reply_t r={.action=CDC_STALL,.address=-1,.configuration=-1,.halt_ep=-1};
 uint16_t v=rd(q+2),i=rd(q+4),n=rd(q+6); int e=ep_index(i);
 if(q[0]==0x80 && q[1]==6) {
  const uint8_t *d=0; unsigned len=0;
  if(v==0x100 && !i){d=cdc_device_descriptor;len=18;}
  if(v==0x200 && !i){d=cdc_config_descriptor;len=CDC_CONFIG_LEN;}
  if((v>>8)==3 && ((v&255)==0 ? i==0 : (i==0x409 || i==0))) {
   const char *str=0;
   switch(v&255){case 0:r.data[0]=4;r.data[1]=3;r.data[2]=9;r.data[3]=4;len=4;break;
    case 1:str="RKMoon";break;case 2:str="RKMoon RP2350 Control";break;
    case 3:d=s->serial;len=34;break;case 4:str="RKMoon Control CDC";break;default:break;}
   if(str){len=2+(unsigned)strlen(str)*2;r.data[0]=(uint8_t)len;r.data[1]=3;for(unsigned k=0;str[k];k++)r.data[2+k*2]=(uint8_t)str[k];}
  }
  if(len){if(d)memcpy(r.data,d,len);r.len=(uint16_t)(n<len?n:len);r.zlp=len<n && len%64==0;r.action=CDC_IN;}
 } else if(q[0]==0 && q[1]==5 && v<128 && !i && !n && !s->configuration) {
  r.action=CDC_STATUS;r.address=v;
 } else if(q[0]==0 && q[1]==9 && v<=1 && !i && !n) {
  r.action=CDC_STATUS;r.configuration=v;
 } else if(q[0]==0x80 && q[1]==8 && !v && !i && n==1) {
  r.action=CDC_IN;r.len=1;r.data[0]=s->configuration;
 } else if(q[1]==0 && !v && n==2 &&
   ((q[0]==0x80 && !i) || (q[0]==0x81 && s->configuration && i<2) ||
    (q[0]==0x82 && (i==0 || i==0x80 || (s->configuration && e>=0))))) {
  r.action=CDC_IN;r.len=2;r.data[0]=q[0]==0x80?1:(q[0]==0x82 && e>=0 && s->halt[e]?1:0);
 } else if(q[0]==0x02 && (q[1]==1 || q[1]==3) && !v && !n && s->configuration && e>=0) {
  r.action=CDC_STATUS;r.halt_ep=i;r.halt_set=q[1]==3;
 } else if(q[0]==0x81 && q[1]==10 && !v && i<2 && n==1 && s->configuration) {
  r.action=CDC_IN;r.len=1;
 } else if(q[0]==1 && q[1]==11 && !v && i<2 && !n && s->configuration) {
  r.action=CDC_STATUS;
 } else if(s->configuration && i==0 && !v && q[0]==0xa1 && q[1]==0x21 && n==7) {
  r.action=CDC_IN;r.len=7;memcpy(r.data,s->line,7);
 } else if(s->configuration && i==0 && !v && q[0]==0x21 && q[1]==0x20 && n==7) {
  r.action=CDC_LINE_OUT;r.len=7;
 } else if(s->configuration && i==0 && q[0]==0x21 && q[1]==0x22 && !n && !(v&~3u)) {
  s->control_lines=v;r.action=CDC_STATUS;
 }
 return r;
}

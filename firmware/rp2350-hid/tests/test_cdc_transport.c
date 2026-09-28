// SPDX-License-Identifier: GPL-3.0-or-later
#define RKMOON_CDC_HOST_TEST 1
#include "../src/pio_cdc.c"
#include <stdio.h>
static void setup(uint8_t type,uint8_t req,uint16_t value,uint16_t index,uint16_t len) {
 uint8_t q[]={type,req,(uint8_t)value,(uint8_t)(value>>8),(uint8_t)index,(uint8_t)(index>>8),(uint8_t)len,(uint8_t)(len>>8)};
 ep(0)->has_transfer=ep(0x80)->has_transfer=false;ep(0)->stalled=ep(0x80)->stalled=false;
 root.setup_packet=q;root.ints=PIO_USB_INTS_SETUP_REQ_BITS;pio_usb_device_irq_handler(0);
}
static void done(uint8_t a,uint16_t n){ep(a)->has_transfer=false;ep(a)->actual_len=n;root.ep_complete=1u<<(((a&127)<<1)|(a>>7));pio_usb_device_irq_handler(0);}
int main(void) {
 uint8_t id[8]={0};pio_cdc_init(id);
 setup(0x80,6,0x200,0,255);assert(ep0_stage==EP_DATA_IN && transfer_len[0x80]==75);
 done(0x80,75);assert(ep0_stage==EP_STATUS_OUT && transfer_len[0]==0);done(0,0);assert(ep0_stage==EP_IDLE);
 setup(0x80,6,0x100,0,8);assert(transfer_len[0x80]==8);
 setup(0,5,5,0,0);assert(address_pending==5 && ep0_stage==EP_STATUS_IN);done(0x80,0);
 setup(0,9,1,0,0);assert(!control.configuration);done(0x80,0);assert(control.configuration==1);
 setup(0x21,0x20,0,0,7);assert(ep0_stage==EP_DATA_OUT && !ep(0x80)->has_transfer);
 uint8_t line[]={0x80,0x25,0,0,0,0,8};memcpy(transfer_buf[0],line,7);done(0,7);
 assert(ep0_stage==EP_STATUS_IN && !memcmp(control.line,line,7));done(0x80,0);
 setup(0x21,0x20,0,0,7);done(0,6);assert(ep(0)->stalled && ep(0x80)->stalled);
 setup(0xa1,0x21,0,0,7);assert(!memcmp(transfer_buf[0x80],line,7));done(0x80,7);done(0,0);
 // Explicit control-IN ZLP path, also used if a future descriptor is 64N.
 setup(0x80,6,0x200,0,255);reply.zlp=true;done(0x80,75);assert(ep0_stage==EP_DATA_IN && transfer_len[0x80]==0);
 done(0x80,0);assert(ep0_stage==EP_STATUS_OUT);done(0,0);
 // Bulk-IN full packet completion emits exactly one terminating ZLP.
 in_zlp=true;unsigned count=transfer_count[CDC_EP_IN];done(CDC_EP_IN,64);
 assert(transfer_count[CDC_EP_IN]==count+1 && transfer_len[CDC_EP_IN]==0 && !in_zlp);
 done(CDC_EP_IN,0);assert(transfer_count[CDC_EP_IN]==count+1);
 ready=true;memset(out_buf,0x5a,64);done(CDC_EP_OUT,64);assert(spsc_count(&rx)==64);
 // DTR drop invalidates, core0 drains old RX and acknowledges the generation.
 setup(0x21,0x22,1,0,0);done(0x80,0);last_lines=1;ready=true;atomic_store(&online,1);
 setup(0x21,0x22,0,0,0);assert(!ready && !atomic_load(&online));done(0x80,0);
 bridge_t b;bridge_io_t io={0};bridge_init(&b,&io,id);b.active_ch=BRIDGE_CH_PIO_CDC;
 pio_cdc_pump(&b,0);assert(b.active_ch==BRIDGE_CH_NONE && !spsc_count(&rx));
 assert(atomic_load(&acknowledged)==atomic_load(&generation));
 setup(2,3,0,CDC_EP_IN,0);done(0x80,0);assert(ep(CDC_EP_IN)->stalled);
 ep(CDC_EP_IN)->data_id=1;setup(2,1,0,CDC_EP_IN,0);done(0x80,0);assert(!ep(CDC_EP_IN)->stalled && !ep(CDC_EP_IN)->data_id);
 root.ints=PIO_USB_INTS_RESET_END_BITS;pio_usb_device_irq_handler(0);assert(!control.configuration && !ready);
 // A stalled core1 must request an epoch reset before queued bytes resume.
 b.active_ch=BRIDGE_CH_PIO_CDC;fake_us=20000;atomic_store(&heartbeat,0);
 pio_cdc_pump(&b,20);assert(b.active_ch==BRIDGE_CH_NONE && atomic_load(&fault_requested));
 puts("CDC transport simulation: EP0 stages/abort/STALL, config, bulk/ZLP, DTR epoch, halt/reset: PASS (no hardware)");
}

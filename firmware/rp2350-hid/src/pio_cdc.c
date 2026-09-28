// SPDX-License-Identifier: GPL-3.0-or-later
// Independent CDC device stack. No TinyUSB symbols or second dcd instance.
#include "pio_cdc.h"
#include "cdc_control.h"
#include "spsc.h"
#ifdef RKMOON_CDC_HOST_TEST
#include "cdc_transport_stub.h"
#else
#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#endif
#include <string.h>

extern volatile uint32_t rkmoon_pio_last_sof_us;
static cdc_control_t control;
static cdc_reply_t reply;
static uint8_t ep0_buf[128], out_buf[64], in_buf[64], notification[10];
static enum { EP_IDLE, EP_DATA_IN, EP_DATA_OUT, EP_STATUS_IN, EP_STATUS_OUT } ep0_stage;
static spsc_t rx, tx;
// epoch handshake: core1 publishes a new generation (offline), core0 drains
// its RX and acknowledges, core1 drains its TX before publishing online.
// No side ever writes the other side's ring cursor.
static atomic_uint generation, acknowledged, online, heartbeat, fault_requested;
static unsigned seen_generation;
static bool ready, in_zlp, notify_pending;
static uint16_t last_lines;
static bool last_bus;

static endpoint_t *ep(uint8_t address) {return pio_usb_device_get_endpoint_by_address(address);}
static void xfer(uint8_t address,uint8_t *data,uint16_t len) {
    if (!pio_usb_device_transfer(address,data,len)) panic("CDC endpoint busy");
}
static void invalidate(void) {
    atomic_store_explicit(&online,0,memory_order_release);
    atomic_fetch_add_explicit(&generation,1,memory_order_release);
    ready=false;
    ep(CDC_EP_OUT)->has_transfer=false;
    ep(CDC_EP_IN)->has_transfer=false;
    in_zlp=false;
}
static void stall(void) {
    ep(0)->has_transfer=ep(0x80)->has_transfer=false;
    ep(0)->stalled=ep(0x80)->stalled=true;
    ep0_stage=EP_IDLE;
}
static void configure(uint8_t value) {
    invalidate();
    control.configuration=value;control.control_lines=0;
    memset(control.halt,0,sizeof(control.halt));
    for(unsigned a=2;a<6;a++)memset(PIO_USB_ENDPOINT(a),0,sizeof(endpoint_t));
    if(value) {
        for(unsigned i=0;i<CDC_CONFIG_LEN;i+=cdc_config_descriptor[i])
            if(cdc_config_descriptor[i+1]==5)pio_usb_device_endpoint_open(cdc_config_descriptor+i);
    }
    notify_pending=value!=0;
}
// Called synchronously by the patched packet IRQ, with core1 IRQ exclusion.
void pio_usb_device_irq_handler(uint8_t root_idx) {
    root_port_t *r=PIO_USB_ROOT_PORT(root_idx);
    uint32_t ints=r->ints, completed=r->ep_complete;
    r->ints=0;r->ep_complete=0;
    if(ints & PIO_USB_INTS_RESET_END_BITS) {
        cdc_control_reset(&control);invalidate();ep0_stage=EP_IDLE;last_lines=0;
        return;
    }
    if(ints & PIO_USB_INTS_SETUP_REQ_BITS) {
        // A new SETUP cancels both old EP0 stages, including queued completions.
        completed &= ~3u;
        reply=cdc_control_setup(&control,r->setup_packet);
        memcpy(ep0_buf,reply.data,sizeof(ep0_buf));
        switch(reply.action) {
        case CDC_IN:
            ep0_stage=EP_DATA_IN;xfer(0x80,ep0_buf,reply.len);break;
        case CDC_STATUS:
            if(reply.address>=0)pio_usb_device_set_address((uint8_t)reply.address);
            ep0_stage=EP_STATUS_IN;xfer(0x80,ep0_buf,0);break;
        case CDC_LINE_OUT:
            ep0_stage=EP_DATA_OUT;xfer(0,ep0_buf,7);break;
        default:stall();break;
        }
        if(last_lines!=control.control_lines) {
            if(!(control.control_lines&1))invalidate();
            last_lines=control.control_lines;notify_pending=true;
        }
    }
    if(completed & 1u) {
        if(ep0_stage==EP_DATA_OUT) {
            if(cdc_control_line(&control,ep0_buf,ep(0)->actual_len)) {
                ep0_stage=EP_STATUS_IN;xfer(0x80,ep0_buf,0);
            } else stall();
        } else if(ep0_stage==EP_STATUS_OUT)ep0_stage=EP_IDLE;
    }
    if(completed & 2u) {
        if(ep0_stage==EP_DATA_IN) {
            if(reply.zlp){reply.zlp=false;xfer(0x80,ep0_buf,0);}
            else {ep0_stage=EP_STATUS_OUT;xfer(0,ep0_buf,0);}
        } else if(ep0_stage==EP_STATUS_IN) {
            if(reply.configuration>=0)configure((uint8_t)reply.configuration);
            if(reply.halt_ep>=0) {
                uint8_t a=(uint8_t)reply.halt_ep;
                unsigned k=a==CDC_EP_NOTIFY?0:a==CDC_EP_OUT?1:2;
                control.halt[k]=reply.halt_set;
                ep(a)->has_transfer=false;ep(a)->stalled=reply.halt_set;
                if(!reply.halt_set)ep(a)->data_id=0;
                if(a==CDC_EP_IN)in_zlp=false;
            }
            ep0_stage=EP_IDLE;
        }
    }
    if(completed & (1u<<4)) {
        // OUT is only armed when there is room for the whole packet.
        if(ready && !spsc_write(&rx,out_buf,ep(CDC_EP_OUT)->actual_len)) {
            invalidate(); // invariant violation: fail closed, never replay bytes
        }
    }
    if(completed & (1u<<5)) {
        if(in_zlp) {in_zlp=false;xfer(CDC_EP_IN,in_buf,0);}
    }
}

static void core1_main(void) {
    pio_usb_configuration_t cfg=PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp=12;cfg.pinout=PIO_USB_PINOUT_DPDM;
    cfg.pio_tx_num=0;cfg.sm_tx=0;cfg.tx_ch=0;
    cfg.pio_rx_num=1;cfg.sm_rx=0;cfg.sm_eop=1;
    cfg.skip_alarm_pool=true;
    const usb_descriptor_buffers_t unused={0};
    pio_usb_device_init(&cfg,&unused);
    for(;;) {
        pio_usb_device_task();
        uint32_t saved=save_and_disable_interrupts();
        uint32_t now=time_us_32();
        atomic_store_explicit(&heartbeat,now,memory_order_release);
        if(atomic_exchange_explicit(&fault_requested,0,memory_order_acq_rel))invalidate();
        bool bus=(uint32_t)(now-rkmoon_pio_last_sof_us)<3000;
        if(last_bus && !bus)invalidate();
        last_bus=bus;
        bool wanted=control.configuration && (control.control_lines&1) && bus;
        if(wanted && !ready && atomic_load_explicit(&generation,memory_order_acquire)==
                                      atomic_load_explicit(&acknowledged,memory_order_acquire)) {
            uint8_t junk[64];while(spsc_read(&tx,junk,sizeof(junk))){}
            ready=true;atomic_store_explicit(&online,1,memory_order_release);
        }
        if(ready && wanted) {
            if(!ep(CDC_EP_OUT)->has_transfer && !ep(CDC_EP_OUT)->stalled && spsc_free(&rx)>=64)
                xfer(CDC_EP_OUT,out_buf,64);
            if(!ep(CDC_EP_IN)->has_transfer && !ep(CDC_EP_IN)->stalled) {
                unsigned n=spsc_read(&tx,in_buf,sizeof(in_buf));
                if(n){in_zlp=n==64;xfer(CDC_EP_IN,in_buf,(uint16_t)n);}
            }
        }
        if(control.configuration && bus && notify_pending && !ep(CDC_EP_NOTIFY)->has_transfer && !ep(CDC_EP_NOTIFY)->stalled) {
            const uint8_t msg[]={0xa1,0x20,0,0,0,0,2,0,0,0};
            memcpy(notification,msg,10);notification[8]=(control.control_lines&1)?3:0;
            xfer(CDC_EP_NOTIFY,notification,10);notify_pending=false;
        }
        restore_interrupts(saved);
        tight_loop_contents();
    }
}
void pio_cdc_init(const uint8_t serial[8]) {
    cdc_control_init(&control,serial);
    atomic_store(&generation,1);
    multicore_launch_core1(core1_main);
}
bool pio_cdc_write(const uint8_t *data,size_t len) {
    return len<=PROTO_MAX_FRAME && atomic_load_explicit(&online,memory_order_acquire) &&
           spsc_write(&tx,data,(unsigned)len);
}
void pio_cdc_pump(bridge_t *b,uint32_t now) {
    unsigned g=atomic_load_explicit(&generation,memory_order_acquire);
    if(g!=seen_generation) {
        bridge_channel_lost(b,BRIDGE_CH_PIO_CDC);
        uint8_t junk[64];while(spsc_read(&rx,junk,sizeof(junk))){}
        seen_generation=g;atomic_store_explicit(&acknowledged,g,memory_order_release);
    }
    if((uint32_t)(time_us_32()-atomic_load_explicit(&heartbeat,memory_order_acquire))>10000) {
        atomic_store_explicit(&fault_requested,1,memory_order_release);
        bridge_channel_lost(b,BRIDGE_CH_PIO_CDC);return;
    }
    if(atomic_load_explicit(&fault_requested,memory_order_acquire) ||
       !atomic_load_explicit(&online,memory_order_acquire))return;
    // Reserve worst-case replies from a resynchronizing maximum-size frame.
    // One byte can expose multiple corrupt inner frames; 512 bytes is ample.
    for(unsigned i=0;i<256 && bridge_can_accept(b) && spsc_free(&tx)>=512;i++) {
        uint8_t byte;
        if(atomic_load_explicit(&generation,memory_order_acquire)!=seen_generation)break;
        if(!spsc_read(&rx,&byte,1))break;
        bridge_rx_byte(b,BRIDGE_CH_PIO_CDC,byte,now);
    }
}

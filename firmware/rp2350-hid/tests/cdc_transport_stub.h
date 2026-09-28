// Host simulation of the PIO controller, not a USB electrical test.
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct { bool has_transfer,stalled; uint8_t data_id; uint16_t actual_len; } endpoint_t;
typedef struct { uint32_t ints,ep_complete;uint8_t *setup_packet; } root_port_t;
typedef struct {int pin_dp,pinout,pio_tx_num,sm_tx,tx_ch,pio_rx_num,sm_rx,sm_eop;bool skip_alarm_pool;} pio_usb_configuration_t;
typedef struct {int unused;} usb_descriptor_buffers_t;
#define PIO_USB_DEFAULT_CONFIG {0}
#define PIO_USB_PINOUT_DPDM 0
#define PIO_USB_INTS_RESET_END_BITS 1
#define PIO_USB_INTS_SETUP_REQ_BITS 2
static endpoint_t endpoints[32];
static root_port_t root;
#define PIO_USB_ROOT_PORT(i) (&root)
#define PIO_USB_ENDPOINT(i) (&endpoints[i])
static uint16_t transfer_len[256];
static uint8_t *transfer_buf[256];
static unsigned transfer_count[256];
static uint32_t fake_us;
volatile uint32_t rkmoon_pio_last_sof_us;
static endpoint_t *pio_usb_device_get_endpoint_by_address(uint8_t a){return &endpoints[((a&127)<<1)|(a>>7)];}
static bool pio_usb_device_transfer(uint8_t a,uint8_t *b,uint16_t n){endpoint_t *e=pio_usb_device_get_endpoint_by_address(a);if(e->has_transfer)return false;e->has_transfer=true;transfer_len[a]=n;transfer_buf[a]=b;transfer_count[a]++;return true;}
static int address_pending;
static void pio_usb_device_set_address(uint8_t a){address_pending=a;}
static bool pio_usb_device_endpoint_open(const uint8_t *d){(void)d;return true;}
static void pio_usb_device_init(const pio_usb_configuration_t *c,const usb_descriptor_buffers_t *b){(void)c;(void)b;}
static void pio_usb_device_task(void){}
static uint32_t time_us_32(void){return fake_us;}
static uint32_t save_and_disable_interrupts(void){return 0;}
static void restore_interrupts(uint32_t n){(void)n;}
static void panic(const char *s){(void)s;abort();}
static void multicore_launch_core1(void (*fn)(void)){(void)fn;}
static void tight_loop_contents(void){}

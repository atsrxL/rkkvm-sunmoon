// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RKMOON_CDC_CONTROL_H
#define RKMOON_CDC_CONTROL_H
#include <stdbool.h>
#include <stdint.h>
#define CDC_CONFIG_LEN 75
#define CDC_EP_NOTIFY 0x81
#define CDC_EP_OUT 0x02
#define CDC_EP_IN 0x82
typedef enum { CDC_STALL, CDC_IN, CDC_STATUS, CDC_LINE_OUT } cdc_action_t;
typedef struct {
    uint8_t configuration, line[7], serial[34];
    uint16_t control_lines;
    bool halt[3];
} cdc_control_t;
typedef struct {
    cdc_action_t action;
    uint8_t data[128];
    uint16_t len;
    bool zlp;
    int address, configuration, halt_ep;
    bool halt_set;
} cdc_reply_t;
extern const uint8_t cdc_device_descriptor[18];
extern const uint8_t cdc_config_descriptor[CDC_CONFIG_LEN];
void cdc_control_init(cdc_control_t *s, const uint8_t serial[8]);
void cdc_control_reset(cdc_control_t *s);
cdc_reply_t cdc_control_setup(cdc_control_t *s, const uint8_t setup[8]);
bool cdc_control_line(cdc_control_t *s, const uint8_t *data, unsigned len);
#endif

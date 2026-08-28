#ifndef LUNAR_USB_DESCRIPTORS_H
#define LUNAR_USB_DESCRIPTORS_H

#include <stdint.h>

extern uint8_t tud_network_mac_address[6];

void usb_identity_init(void);

#endif

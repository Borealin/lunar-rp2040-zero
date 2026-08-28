#include "usb_descriptors.h"

#include <string.h>

#include "pico/unique_id.h"
#include "pico/usb_reset.h"
#include "tusb.h"

#define USB_VID 0xCAFEu
#define USB_PID 0x4020u
#define USB_BCD 0x0100u

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC,
    STRID_RESET,
};

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_RESET,
    ITF_NUM_TOTAL,
};

enum {
    EPNUM_CDC_NOTIFICATION = 0x81,
    EPNUM_CDC_OUT = 0x02,
    EPNUM_CDC_IN = 0x82,
};

#define CONFIG_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_RPI_RESET_DESC_LEN)

static pico_unique_board_id_t board_id;

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIFICATION, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_RPI_RESET_DESCRIPTOR(ITF_NUM_RESET, STRID_RESET),
};

static const char *const string_descriptors[] = {
    [STRID_LANGID] = (const char[]){0x09, 0x04},
    [STRID_MANUFACTURER] = "Lunar RP2040",
    [STRID_PRODUCT] = "Lunar Ambient Light Sensor",
    [STRID_SERIAL] = NULL,
    [STRID_CDC] = "Lunar Sensor Protocol",
    [STRID_RESET] = "RP2040 BOOTSEL Reset",
};

static uint16_t string_buffer[33];

void usb_identity_init(void) {
    pico_get_unique_board_id(&board_id);
}

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    return index == 0u ? configuration_descriptor : NULL;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
    (void)language_id;
    size_t count = 0u;

    if (index == STRID_LANGID) {
        memcpy(&string_buffer[1], string_descriptors[STRID_LANGID], 2u);
        count = 1u;
    } else if (index == STRID_SERIAL) {
        static const char hex[] = "0123456789ABCDEF";
        for (size_t i = 0u; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; ++i) {
            string_buffer[1u + count++] = (uint16_t)hex[board_id.id[i] >> 4u];
            string_buffer[1u + count++] = (uint16_t)hex[board_id.id[i] & 0x0Fu];
        }
    } else {
        if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0]) ||
            string_descriptors[index] == NULL) {
            return NULL;
        }
        const char *text = string_descriptors[index];
        count = strlen(text);
        if (count > 32u) {
            count = 32u;
        }
        for (size_t i = 0u; i < count; ++i) {
            string_buffer[1u + i] = (uint16_t)text[i];
        }
    }

    string_buffer[0] =
        (uint16_t)((TUSB_DESC_STRING << 8u) | (uint16_t)(2u * count + 2u));
    return string_buffer;
}

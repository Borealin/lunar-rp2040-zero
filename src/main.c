#include <stdio.h>

#include "bsp/board_api.h"
#include "hardware/i2c.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "serial_protocol.h"
#include "tsl2591.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "ws2812_off.h"

int main(void) {
    board_init();
    stdio_init_all();
    ws2812_drive_black(PICO_DEFAULT_WS2812_PIN);

    usb_identity_init();
    const tusb_rhport_init_t usb_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    if (!tusb_init(0u, &usb_init)) {
        panic("TinyUSB init failed");
    }

    serial_protocol_init();

    tsl2591_t sensor;
    tsl2591_init(&sensor, i2c1, PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN);

    while (true) {
        tud_task();

        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        tsl2591_task(&sensor, now_ms);
        serial_protocol_task(&sensor, now_ms);

        if (serial_protocol_bootloader_due(now_ms)) {
            reset_usb_boot(0u, 0u);
        }

        tight_loop_contents();
    }
}

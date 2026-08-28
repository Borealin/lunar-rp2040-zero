#include "serial_protocol.h"

#include <stdio.h>
#include <string.h>

#include "tusb.h"

#define PROTOCOL_TX_PERIOD_MS 1000u
#define PROTOCOL_BOOTSEL_DELAY_MS 500u
#define PROTOCOL_LINE_CAPACITY 192u

#define SENSOR_FLAG_DETECTED 0x01u
#define SENSOR_FLAG_VALID 0x02u
#define SENSOR_FLAG_FRESH 0x04u

static char rx_line[PROTOCOL_LINE_CAPACITY];
static size_t rx_length;
static uint32_t last_tx_ms;
static uint32_t sequence;
static bool bootsel_pending;
static uint32_t bootsel_at_ms;

static uint16_t crc16_ccitt(const char *data, size_t length) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)(uint8_t)data[i] << 8u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ? (uint16_t)((crc << 1u) ^ 0x1021u)
                                         : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static bool parse_hex16(const char *text, uint16_t *value) {
    uint16_t result = 0u;
    for (size_t i = 0u; i < 4u; ++i) {
        const char c = text[i];
        uint8_t nibble;
        if (c >= '0' && c <= '9') {
            nibble = (uint8_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            nibble = (uint8_t)(c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            nibble = (uint8_t)(c - 'a' + 10);
        } else {
            return false;
        }
        result = (uint16_t)((result << 4u) | nibble);
    }
    *value = result;
    return true;
}

static bool write_payload(const char *payload) {
    char frame[PROTOCOL_LINE_CAPACITY];
    const size_t payload_length = strlen(payload);
    const uint16_t crc = crc16_ccitt(payload, payload_length);
    const int frame_length =
        snprintf(frame, sizeof(frame), "@%s*%04X\r\n", payload, crc);
    if (frame_length <= 0 || (size_t)frame_length >= sizeof(frame) ||
        !tud_cdc_connected() || tud_cdc_write_available() < (uint32_t)frame_length) {
        return false;
    }
    tud_cdc_write(frame, (uint32_t)frame_length);
    tud_cdc_write_flush();
    return true;
}

static bool validate_line(char *line, char **payload) {
    if (line[0] != '@') {
        return false;
    }
    char *separator = strrchr(line, '*');
    if (separator == NULL || strlen(separator + 1) != 4u) {
        return false;
    }
    uint16_t received_crc;
    if (!parse_hex16(separator + 1, &received_crc)) {
        return false;
    }
    *separator = '\0';
    *payload = line + 1;
    return crc16_ccitt(*payload, strlen(*payload)) == received_crc;
}

static void handle_line(char *line, uint32_t now_ms) {
    char *payload;
    if (!validate_line(line, &payload)) {
        return;
    }

    unsigned long nonce = 0u;
    char command[24] = {0};
    if (sscanf(payload, "LS1,C,%lu,%23[A-Z]", &nonce, command) != 2) {
        return;
    }

    if (strcmp(command, "BOOTSEL") == 0) {
        char ack[80];
        snprintf(ack, sizeof(ack), "LS1,A,%lu,BOOTSEL,OK", nonce);
        if (write_payload(ack)) {
            bootsel_pending = true;
            bootsel_at_ms = now_ms + PROTOCOL_BOOTSEL_DELAY_MS;
        }
    }
}

static void receive_commands(uint32_t now_ms) {
    while (tud_cdc_available() != 0u) {
        const char c = (char)tud_cdc_read_char();
        if (c == '\n') {
            rx_line[rx_length] = '\0';
            handle_line(rx_line, now_ms);
            rx_length = 0u;
        } else if (c != '\r') {
            if (rx_length + 1u < sizeof(rx_line)) {
                rx_line[rx_length++] = c;
            } else {
                rx_length = 0u;
            }
        }
    }
}

static void send_sensor_state(const tsl2591_t *sensor, uint32_t now_ms) {
    uint8_t flags = 0u;
    if (sensor->detected) {
        flags |= SENSOR_FLAG_DETECTED;
    }
    if (sensor->reading_valid) {
        flags |= SENSOR_FLAG_VALID;
    }
    const bool fresh = tsl2591_has_fresh_reading(sensor, now_ms);
    if (fresh) {
        flags |= SENSOR_FLAG_FRESH;
    }

    uint32_t lux_millilux = 0u;
    if (fresh) {
        const float scaled = tsl2591_lux(sensor) * 1000.0f;
        lux_millilux = scaled >= (float)UINT32_MAX
                            ? UINT32_MAX
                            : (uint32_t)(scaled + 0.5f);
    }

    char payload[160];
    snprintf(payload, sizeof(payload),
             "LS1,S,%lu,%lu,%lu,%u,%u,%u,%u,%u,%lu",
             (unsigned long)sequence++, (unsigned long)now_ms,
             (unsigned long)lux_millilux, sensor->raw_full, sensor->raw_ir,
             tsl2591_gain_multiplier(sensor),
             (unsigned int)(100u * (sensor->integration_index + 1u)), flags,
             (unsigned long)sensor->error_count);
    write_payload(payload);
}

void serial_protocol_init(void) {
    rx_length = 0u;
    last_tx_ms = 0u;
    sequence = 0u;
    bootsel_pending = false;
    bootsel_at_ms = 0u;
}

void serial_protocol_task(const tsl2591_t *sensor, uint32_t now_ms) {
    receive_commands(now_ms);
    if ((uint32_t)(now_ms - last_tx_ms) >= PROTOCOL_TX_PERIOD_MS) {
        last_tx_ms = now_ms;
        send_sensor_state(sensor, now_ms);
    }
}

bool serial_protocol_bootloader_due(uint32_t now_ms) {
    return bootsel_pending && (int32_t)(now_ms - bootsel_at_ms) >= 0;
}

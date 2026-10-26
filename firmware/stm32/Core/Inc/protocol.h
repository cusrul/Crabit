#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

// 1. COBS 编码 (消除 0x00)
inline size_t cobs_encode(const uint8_t *ptr, size_t length, uint8_t *dst) {
    size_t read_index = 0, write_index = 1, code_index = 0;
    uint8_t code = 1;
    while (read_index < length) {
        if (ptr[read_index] == 0) {
            dst[code_index] = code;
            code = 1;
            code_index = write_index++;
            read_index++;
        } else {
            dst[write_index++] = ptr[read_index++];
            code++;
            if (code == 0xFF) {
                dst[code_index] = code;
                code = 1;
                code_index = write_index++;
            }
        }
    }
    dst[code_index] = code;
    return write_index;
}

// 2. COBS 解码 (还原真实数据)
inline size_t cobs_decode(const uint8_t *ptr, size_t length, uint8_t *dst) {
    size_t read_index = 0, write_index = 0;
    uint8_t code, i;
    while (read_index < length) {
        code = ptr[read_index];
        if (read_index + code > length && code != 1) return 0;
        read_index++;
        for (i = 1; i < code; i++) dst[write_index++] = ptr[read_index++];
        if (code != 0xFF && read_index != length) dst[write_index++] = '\0';
    }
    return write_index;
}

// 3. 极速纯软件 CRC16 (CCITT-FALSE)
inline uint16_t calculate_crc16(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

#endif
#pragma once

/*
 * LdsePacket.h - compact LDSE frame format for the LoRa radio link.
 *
 * Mirrors the ns-3 LdseTag / LdseHeader design (origin id, layer, hop count)
 * and the message builders from LdseChannelManager.
 *
 * Frame layout (payload of the LoRa packet):
 *   byte 0        : magic 0x4C ('L')
 *   byte 1        : version
 *   byte 2        : message type
 *   byte 3        : source id
 *   byte 4        : destination id (0xFF = broadcast)
 *   byte 5        : origin id (original sender, survives forwarding)
 *   byte 6        : hop count
 *   byte 7        : layer
 *   bytes 8..11   : timestamp us (32-bit)  [FTSP sync]
 *   bytes 12..13  : sequence number (16-bit)
 *   bytes 14..15  : energy percentage (8-bit) + rssi dbm (8-bit, on RX fill)
 *   bytes 16..    : payload
 */

#include "LdseCompat.h"

namespace ldse
{

enum MessageType : uint8_t
{
    MSG_LAYER_INIT = 0x01, // gateway -> nodes: assign layer
    MSG_SYNC = 0x02,       // FTSP sync beacon
    MSG_RREQ = 0x03,       // route request (discovery probe)
    MSG_RREP = 0x04,       // route reply
    MSG_HANDSHAKE = 0x05,  // route establishment
    MSG_DATA = 0x06,       // sensor data
    MSG_ACK = 0x07,        // data acknowledgment
    MSG_FIRE_ALERT = 0x08  // fire-risk wake-up packet
};

constexpr uint8_t LDSE_MAGIC = 0x4C;
constexpr uint8_t LDSE_VERSION = 0x01;
constexpr uint8_t LDSE_BROADCAST = 0xFF;
constexpr uint8_t LDSE_HEADER_LEN = 16;
constexpr uint8_t LDSE_PAYLOAD_MAX = 48;
constexpr uint8_t LDSE_FRAME_MAX = LDSE_HEADER_LEN + LDSE_PAYLOAD_MAX;

struct LdsePacket
{
    uint8_t type = MSG_DATA;
    uint8_t srcId = 0;
    uint8_t dstId = LDSE_BROADCAST;
    uint8_t originId = 0;
    uint8_t hopCount = 0;
    uint8_t layer = 0;
    uint32_t timestampUs = 0;
    uint16_t seq = 0;
    uint8_t energyPct = 100;
    int8_t rssiDbm = 0;
    uint8_t payloadLen = 0;
    uint8_t payload[LDSE_PAYLOAD_MAX] = {0};

    /** Serialize the frame. @return total length. */
    uint8_t Encode(uint8_t* buf) const
    {
        buf[0] = LDSE_MAGIC;
        buf[1] = LDSE_VERSION;
        buf[2] = type;
        buf[3] = srcId;
        buf[4] = dstId;
        buf[5] = originId;
        buf[6] = hopCount;
        buf[7] = layer;
        buf[8] = (timestampUs >> 24) & 0xFF;
        buf[9] = (timestampUs >> 16) & 0xFF;
        buf[10] = (timestampUs >> 8) & 0xFF;
        buf[11] = timestampUs & 0xFF;
        buf[12] = (seq >> 8) & 0xFF;
        buf[13] = seq & 0xFF;
        buf[14] = energyPct;
        buf[15] = static_cast<uint8_t>(rssiDbm);
        uint8_t n = ldse_min_u8(payloadLen, LDSE_PAYLOAD_MAX);
        memcpy(buf + LDSE_HEADER_LEN, payload, n);
        return LDSE_HEADER_LEN + n;
    }

    /** Parse a received frame. @return true if the header is valid. */
    bool Decode(const uint8_t* buf, uint8_t len)
    {
        if (len < LDSE_HEADER_LEN || buf[0] != LDSE_MAGIC || buf[1] != LDSE_VERSION)
        {
            return false;
        }
        type = buf[2];
        srcId = buf[3];
        dstId = buf[4];
        originId = buf[5];
        hopCount = buf[6];
        layer = buf[7];
        timestampUs = (static_cast<uint32_t>(buf[8]) << 24) |
                      (static_cast<uint32_t>(buf[9]) << 16) |
                      (static_cast<uint32_t>(buf[10]) << 8) | buf[11];
        seq = (static_cast<uint16_t>(buf[12]) << 8) | buf[13];
        energyPct = buf[14];
        rssiDbm = static_cast<int8_t>(buf[15]);
        payloadLen = ldse_min_u8(static_cast<uint8_t>(len - LDSE_HEADER_LEN), LDSE_PAYLOAD_MAX);
        memcpy(payload, buf + LDSE_HEADER_LEN, payloadLen);
        return true;
    }
};

} // namespace ldse

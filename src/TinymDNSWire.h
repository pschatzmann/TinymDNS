/**
 * @file TinymDNSWire.h
 * @brief Shared mDNS (RFC 6762) wire-format helpers for TinymDNS.
 *
 * Header-only encoder/decoder for the small subset of DNS needed by
 * multicast DNS: A-record queries and responses, label encoding and
 * name decompression. Used by TinymDNSClient and TinymDNSServer.
 *
 * @author Phil Schatzmann
 */
#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <ctype.h>
#include <string.h>

#ifndef TINYMDNS_MAX_PACKET_SIZE
#define TINYMDNS_MAX_PACKET_SIZE 512
#endif

#ifndef TINYMDNS_NAME_MAX
#define TINYMDNS_NAME_MAX 256
#endif

#ifndef TINYMDNS_DEFAULT_TIMEOUT_MS
#define TINYMDNS_DEFAULT_TIMEOUT_MS 3000
#endif

#ifndef TINYMDNS_DEFAULT_TTL
#define TINYMDNS_DEFAULT_TTL 120
#endif

/// Well-known mDNS UDP port (RFC 6762 section 5.1).
#define TINYMDNS_PORT 5353
/// Default local source port used by TinymDNSClient (must differ from 5353
/// so that responders reply with legacy unicast to this port).
#define TINYMDNS_CLIENT_PORT 5354
/// IPv4 mDNS multicast group 224.0.0.251 (RFC 6762 section 3).
#define TINYMDNS_MULTICAST_IP IPAddress(224, 0, 0, 251)

/**
 * @brief Stateless mDNS message encoder/decoder.
 */
class TinymDNSWire {
 public:
  static constexpr uint16_t kTypeA = 1;        ///< A record (IPv4).
  static constexpr uint16_t kTypeANY = 255;    ///< ANY query type.
  static constexpr uint16_t kClassIN = 1;      ///< Internet class.
  static constexpr uint16_t kUnicastFlag = 0x8000;  ///< QU bit / cache-flush bit.
  static constexpr uint16_t kFlagResponse = 0x8400; ///< QR=1, AA=1.
  static constexpr size_t kHeaderSize = 12;

  /**
   * @brief Build "name.local" from a hostname or an already qualified name.
   * @return false if the name is empty, too long, or contains dots without a .local suffix.
   */
  static bool makeLocalName(const char *name, char *out, size_t cap) {
    if (!name || !*name) return false;
    size_t len = strlen(name);
    if (hasLocalSuffix(name, len)) {
      if (len <= 6 || len + 1 > cap) return false;
      memcpy(out, name, len);
      out[len] = 0;
      return true;
    }
    if (strchr(name, '.')) return false;
    if (len + 6 + 1 > cap) return false;
    memcpy(out, name, len);
    memcpy(out + len, ".local", 7);
    return true;
  }

  /// Case-insensitive comparison of two NUL-terminated names.
  static bool equalsIgnoreCase(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
      if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
      a++;
      b++;
    }
    return *a == 0 && *b == 0;
  }

  /// Encode a dotted name into DNS labels. Returns bytes written or 0 on error.
  static size_t encodeName(const char *name, uint8_t *out, size_t cap) {
    size_t pos = 0;
    const char *p = name;
    while (*p) {
      const char *dot = strchr(p, '.');
      size_t len = dot ? (size_t)(dot - p) : strlen(p);
      if (len == 0 || len > 63) return 0;
      if (pos + 1 + len + 1 > cap) return 0;  // +1 for root terminator
      out[pos++] = (uint8_t)len;
      memcpy(out + pos, p, len);
      pos += len;
      if (!dot) break;
      p = dot + 1;
    }
    if (pos + 1 > cap || pos + 1 > 255) return 0;
    out[pos++] = 0;
    return pos;
  }

  /**
   * @brief Decode a (possibly compressed) name starting at @p pos.
   * @return Offset just after the name in the original record, or 0 on error.
   */
  static size_t decodeName(const uint8_t *msg, size_t len, size_t pos, char *out, size_t outCap) {
    size_t next = 0;
    bool jumped = false;
    size_t outLen = 0;
    int hops = 0;
    if (outCap == 0) return 0;
    while (true) {
      if (pos >= len) return 0;
      uint8_t l = msg[pos];
      if ((l & 0xC0) == 0xC0) {  // compression pointer
        if (pos + 1 >= len) return 0;
        if (!jumped) next = pos + 2;
        jumped = true;
        if (++hops > 32) return 0;  // guard against pointer loops
        pos = (size_t)(((l & 0x3F) << 8) | msg[pos + 1]);
        continue;
      }
      if (l & 0xC0) return 0;  // reserved label types
      if (l == 0) {
        if (!jumped) next = pos + 1;
        break;
      }
      if (pos + 1 + l > len) return 0;
      if (outLen + l + 2 > outCap) return 0;
      if (outLen) out[outLen++] = '.';
      memcpy(out + outLen, msg + pos + 1, l);
      outLen += l;
      pos += 1 + l;
    }
    out[outLen] = 0;
    return next;
  }

  /// Build an A query for @p fqdn (ID 0, as required for mDNS). Returns length or 0.
  static size_t buildQuery(uint8_t *buf, size_t cap, const char *fqdn, uint16_t type) {
    if (cap < kHeaderSize) return 0;
    memset(buf, 0, kHeaderSize);
    writeU16(buf + 4, 1);  // QDCOUNT
    size_t n = encodeName(fqdn, buf + kHeaderSize, cap - kHeaderSize);
    if (!n) return 0;
    size_t pos = kHeaderSize + n;
    if (pos + 4 > cap) return 0;
    writeU16(buf + pos, type);
    writeU16(buf + pos + 2, kClassIN);
    return pos + 4;
  }

  /**
   * @brief Build an A-record response.
   * @param legacy true for legacy unicast replies (echo the question, no cache-flush bit,
   *               TTL capped at 10 s); false for multicast responses (no question section).
   * @return Length of the message or 0 on error.
   */
  static size_t buildAResponse(uint8_t *buf, size_t cap, uint16_t id, const char *fqdn,
                               IPAddress ip, uint32_t ttl, bool legacy) {
    if (cap < kHeaderSize) return 0;
    memset(buf, 0, kHeaderSize);
    writeU16(buf, id);
    writeU16(buf + 2, kFlagResponse);
    writeU16(buf + 4, legacy ? 1 : 0);  // QDCOUNT
    writeU16(buf + 6, 1);               // ANCOUNT
    size_t pos = kHeaderSize;

    size_t n;
    if (legacy) {
      n = encodeName(fqdn, buf + pos, cap - pos);
      if (!n || pos + n + 4 > cap) return 0;
      pos += n;
      writeU16(buf + pos, kTypeA);
      writeU16(buf + pos + 2, kClassIN);
      pos += 4;
    }

    n = encodeName(fqdn, buf + pos, cap - pos);
    if (!n || pos + n + 10 + 4 > cap) return 0;
    pos += n;
    writeU16(buf + pos, kTypeA);
    writeU16(buf + pos + 2, legacy ? kClassIN : (kClassIN | kUnicastFlag));  // cache-flush
    if (legacy && ttl > 10) ttl = 10;
    writeU32(buf + pos + 4, ttl);
    writeU16(buf + pos + 8, 4);  // RDLENGTH
    pos += 10;
    for (int i = 0; i < 4; i++) buf[pos + i] = ip[i];
    pos += 4;
    return pos;
  }

  /** @brief true if @p a and @p b are equal under @p mask. */
  static bool inSubnet(IPAddress a, IPAddress b, IPAddress mask) {
    for (int i = 0; i < 4; i++)
      if ((a[i] & mask[i]) != (b[i] & mask[i])) return false;
    return true;
  }

  /**
   * @brief Parse a response and extract the first A record for @p fqdn.
   * Records outside the subnet given by @p net and @p mask are skipped.
   * An all-zero @p mask accepts any address.
   * @return true if a matching A record was found.
   */
  static bool parseAResponse(const uint8_t *msg, size_t len, const char *fqdn,
                             IPAddress &ip, uint32_t &ttl,
                             IPAddress net = IPAddress(0, 0, 0, 0),
                             IPAddress mask = IPAddress(0, 0, 0, 0)) {
    if (len < kHeaderSize) return false;
    if (!(readU16(msg + 2) & 0x8000)) return false;  // not a response
    uint16_t qd = readU16(msg + 4);
    uint16_t an = readU16(msg + 6);
    uint16_t ns = readU16(msg + 8);
    uint16_t ar = readU16(msg + 10);

    char name[TINYMDNS_NAME_MAX];
    size_t pos = kHeaderSize;
    for (uint16_t i = 0; i < qd; i++) {
      size_t next = decodeName(msg, len, pos, name, sizeof(name));
      if (!next || next + 4 > len) return false;
      pos = next + 4;
    }
    uint32_t records = (uint32_t)an + ns + ar;
    for (uint32_t i = 0; i < records; i++) {
      size_t next = decodeName(msg, len, pos, name, sizeof(name));
      if (!next || next + 10 > len) return false;
      pos = next;
      uint16_t type = readU16(msg + pos);
      uint16_t cls = readU16(msg + pos + 2);
      uint32_t recTtl = readU32(msg + pos + 4);
      uint16_t rdlen = readU16(msg + pos + 8);
      pos += 10;
      if (pos + rdlen > len) return false;
      if (type == kTypeA && rdlen == 4 && (cls & ~kUnicastFlag) == kClassIN &&
          equalsIgnoreCase(name, fqdn)) {
        IPAddress candidate(msg[pos], msg[pos + 1], msg[pos + 2], msg[pos + 3]);
        bool filtered = mask[0] || mask[1] || mask[2] || mask[3];
        if (!filtered || inSubnet(candidate, net, mask)) {
          ip = candidate;
          ttl = recTtl;
          return true;
        }
      }
      pos += rdlen;
    }
    return false;
  }

  static void writeU16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
  }
  static void writeU32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFF);
  }
  static uint16_t readU16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
  static uint32_t readU32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
  }

 protected:
  static bool hasLocalSuffix(const char *name, size_t len) {
    return len >= 6 && equalsIgnoreCase(name + len - 6, ".local");
  }
};

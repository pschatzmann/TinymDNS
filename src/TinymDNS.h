/**
 * @file TinymDNS.h
 * @brief Header-only mDNS (RFC 6762) responder and resolver for Arduino.
 *
 * A single TinymDNS object:
 * - registers a local hostname and IPv4 address (answers "name.local" queries),
 * - announces the registration on the network,
 * - resolves other ".local" names by multicast query.
 *
 * It works with any Arduino UDP implementation (WiFiUDP, EthernetUDP, ...).
 *
 * @code
 * WiFiUDP udp;
 * TinymDNS mdns(udp);
 *
 * void setup() {
 *   mdns.begin("esp32", WiFi.localIP());   // answers esp32.local
 * }
 *
 * void loop() {
 *   mdns.loop();                           // answers queries, periodic announce
 *   IPAddress ip;
 *   if (mdns.resolve("printer", ip)) { ... }
 * }
 * @endcode
 *
 * @author Phil Schatzmann
 */
#pragma once

#include "TinymDNSWire.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <Udp.h>

#if defined(ESP8266)
#include <WiFiUdp.h>
#endif

#ifndef TINYMDNS_DEFAULT_ANNOUNCE_INTERVAL_MS
#define TINYMDNS_DEFAULT_ANNOUNCE_INTERVAL_MS 30000
#endif

/**
 * @brief Outcome of the most recent TinymDNS::resolve() call, via lastResult().
 */
enum class MDNSResult : uint8_t {
  Ok = 0,         ///< Query succeeded and a matching A record was received.
  NotConfigured,  ///< begin() was not successful, or hostname was null.
  InvalidName,    ///< Hostname is empty, too long, or not a single label / .local name.
  SendFailed,     ///< Writing the query to the UDP socket failed.
  Timeout,        ///< No matching response was received in time.
};

/**
 * @brief Header-only mDNS responder and resolver using a generic Arduino UDP object.
 *
 * The object owns one UDP socket on port 5353, joined to the mDNS multicast
 * group 224.0.0.251. Queries for the registered name are answered in loop()
 * and resolve(); resolve() blocks until an answer arrives or the timeout expires.
 */
class TinymDNS {
 public:
  /** @brief Construct an inactive instance; call begin() before use. */
  TinymDNS() = default;

  /**
   * @brief Bind to a UDP implementation without starting. Call begin() to start.
   * @param udp UDP implementation used for the mDNS socket. Must outlive this object.
   */
  explicit TinymDNS(UDP &udp) : udp_(&udp) {}

  /**
   * @brief Construct and start in one step.
   * @param udp UDP implementation used for the mDNS socket. Must outlive this object.
   * @param hostname Local hostname without ".local" (e.g. "esp32").
   * @param address IPv4 address to publish for the hostname.
   * @param ttl Record TTL in seconds.
   */
  TinymDNS(UDP &udp, const char *hostname, IPAddress address, uint32_t ttl = TINYMDNS_DEFAULT_TTL) {
    begin(udp, hostname, address, ttl);
  }

  /**
   * @brief Join the mDNS group, register the hostname and announce it.
   * @return true if the hostname is valid and the socket was opened.
   */
  bool begin(UDP &udp, const char *hostname, IPAddress address, uint32_t ttl = TINYMDNS_DEFAULT_TTL) {
    if (!TinymDNSWire::makeLocalName(hostname, fqdn_, sizeof(fqdn_))) return false;
    udp_ = &udp;
    address_ = address;
    ttl_ = ttl;
#if defined(ESP8266)
    // ESP8266 multicast join is only on WiFiUDP and needs the interface address first
    bool joined = static_cast<WiFiUDP *>(udp_)->beginMulticast(IPAddress(0, 0, 0, 0), TINYMDNS_MULTICAST_IP, TINYMDNS_PORT);
#else
    bool joined = udp_->beginMulticast(TINYMDNS_MULTICAST_IP, TINYMDNS_PORT);
#endif
    if (!joined) {
      started_ = false;
      return false;
    }
    started_ = true;
    announce();
    return true;
  }

  /** @brief Stop the instance and close the socket. */
  void end() {
    if (udp_) udp_->stop();
    started_ = false;
  }

  /** @brief Change the published IPv4 address (e.g. after DHCP renewal). Announces if active. */
  void setAddress(IPAddress address) {
    address_ = address;
    if (started_) announce();
  }

  /** @brief Timeout applied to each resolve() call, in milliseconds. */
  void setTimeout(uint32_t timeoutMs) { timeoutMs_ = timeoutMs; }

  /**
   * @brief Restrict resolve() to answers on the local subnet.
   * An answer is accepted only if it matches the registered address under @p mask.
   * Use WiFi.subnetMask() (or the equivalent). The default 0.0.0.0 accepts any answer.
   */
  void setSubnetMask(IPAddress mask) { subnetMask_ = mask; }

  /** @brief Interval between unsolicited announcements in loop(); 0 disables them. */
  void setAnnounceInterval(uint32_t intervalMs) { announceIntervalMs_ = intervalMs; }

  /** @brief Send an unsolicited multicast announcement of the registered name. */
  bool announce() {
    if (!started_) return false;
    lastAnnounce_ = millis();
    return sendAnswer(TINYMDNS_MULTICAST_IP, TINYMDNS_PORT, 0, false);
  }

  /**
   * @brief Process pending mDNS traffic and periodic announcements. Call from loop().
   */
  void loop() {
    if (!started_) return;
    if (announceIntervalMs_ && millis() - lastAnnounce_ >= announceIntervalMs_) announce();
    poll();
  }

  /**
   * @brief Resolve a ".local" hostname (a bare label gets ".local" appended) to IPv4.
   * Blocks up to the configured timeout. Queries for the local name received meanwhile are still answered.
   * @param hostname Null-terminated hostname, e.g. "printer" or "printer.local".
   * @param result Set to the resolved address on success; unmodified on failure.
   * @return true if an A record was received, false otherwise (see lastResult()).
   */
  bool resolve(const char *hostname, IPAddress &result) {
    if (!started_ || !hostname) return fail(MDNSResult::NotConfigured);

    char name[TINYMDNS_NAME_MAX];
    if (!TinymDNSWire::makeLocalName(hostname, name, sizeof(name))) return fail(MDNSResult::InvalidName);

    uint8_t buf[TINYMDNS_MAX_PACKET_SIZE];
    size_t n = TinymDNSWire::buildQuery(buf, sizeof(buf), name, TinymDNSWire::kTypeA);
    if (!n) return fail(MDNSResult::InvalidName);

    strncpy(queryName_, name, sizeof(queryName_) - 1);
    queryName_[sizeof(queryName_) - 1] = 0;
    queryAnswered_ = false;
    queryActive_ = true;

    bool sent = udp_->beginPacket(TINYMDNS_MULTICAST_IP, TINYMDNS_PORT) &&
                udp_->write(buf, n) == n && udp_->endPacket();
    if (!sent) {
      queryActive_ = false;
      return fail(MDNSResult::SendFailed);
    }

    uint32_t start = millis();
    while (millis() - start < timeoutMs_) {
      poll();
      if (queryAnswered_) {
        queryActive_ = false;
        result = answerIp_;
        lastTtl_ = answerTtl_;
        lastResult_ = MDNSResult::Ok;
        return true;
      }
      delay(1);
    }
    queryActive_ = false;
    return fail(MDNSResult::Timeout);
  }

  /** @brief Outcome of the most recent resolve() call. */
  MDNSResult lastResult() const { return lastResult_; }

  /** @brief TTL (seconds) of the record from the most recent successful resolve(). */
  uint32_t lastTtl() const { return lastTtl_; }

  /** @brief Registered hostname including ".local". */
  const char *fqdn() const { return fqdn_; }

  /** @brief true while the instance is running. */
  bool isActive() const { return started_; }

 protected:
  /** Read and handle at most one pending packet. */
  void poll() {
    int sz = udp_->parsePacket();
    if (sz > 0) handlePacket((size_t)sz);
  }

  void handlePacket(size_t sz) {
    IPAddress from = udp_->remoteIP();
    uint16_t fromPort = udp_->remotePort();

    uint8_t buf[TINYMDNS_MAX_PACKET_SIZE];
    size_t want = sz < sizeof(buf) ? sz : sizeof(buf);
    int got = udp_->read(buf, want);
    while (udp_->available()) udp_->read();  // discard oversized remainder
    if (got <= 0) return;
    size_t n = (size_t)got;

    if (n < TinymDNSWire::kHeaderSize) return;
    uint16_t flags = TinymDNSWire::readU16(buf + 2);

    if (flags & 0x8000) {
      // Response: capture the answer to our pending resolve() query.
      if (queryActive_ && !queryAnswered_) {
        uint32_t ttl = 0;
        IPAddress ip;
        if (TinymDNSWire::parseAResponse(buf, n, queryName_, ip, ttl, address_, subnetMask_)) {
          queryAnswered_ = true;
          answerIp_ = ip;
          answerTtl_ = ttl;
        }
      }
      return;
    }
    if (flags & 0x7800) return;  // only standard queries (opcode 0)

    // Query: answer if it asks for our registered name.
    uint16_t id = TinymDNSWire::readU16(buf);
    uint16_t qd = TinymDNSWire::readU16(buf + 4);
    size_t pos = TinymDNSWire::kHeaderSize;

    bool wantsAnswer = false;
    bool unicastRequested = false;
    char name[TINYMDNS_NAME_MAX];
    for (uint16_t i = 0; i < qd; i++) {
      size_t next = TinymDNSWire::decodeName(buf, n, pos, name, sizeof(name));
      if (!next || next + 4 > n) return;  // malformed
      uint16_t type = TinymDNSWire::readU16(buf + next);
      uint16_t cls = TinymDNSWire::readU16(buf + next + 2);
      pos = next + 4;

      if ((cls & ~TinymDNSWire::kUnicastFlag) != TinymDNSWire::kClassIN) continue;
      if (!TinymDNSWire::equalsIgnoreCase(name, fqdn_)) continue;
      if (type != TinymDNSWire::kTypeA && type != TinymDNSWire::kTypeANY) continue;
      wantsAnswer = true;
      if (cls & TinymDNSWire::kUnicastFlag) unicastRequested = true;
    }
    if (!wantsAnswer) return;

    if (fromPort != TINYMDNS_PORT) {
      // Legacy unicast query (RFC 6762 section 6.7): reply to sender with same ID.
      sendAnswer(from, fromPort, id, true);
    } else if (unicastRequested) {
      sendAnswer(from, TINYMDNS_PORT, 0, false);
    } else {
      sendAnswer(TINYMDNS_MULTICAST_IP, TINYMDNS_PORT, 0, false);
    }
  }

  bool sendAnswer(IPAddress to, uint16_t port, uint16_t id, bool legacy) {
    uint8_t buf[TINYMDNS_MAX_PACKET_SIZE];
    size_t n = TinymDNSWire::buildAResponse(buf, sizeof(buf), id, fqdn_, address_, ttl_, legacy);
    if (!n) return false;
    if (!udp_->beginPacket(to, port)) return false;
    bool ok = udp_->write(buf, n) == n;
    return udp_->endPacket() && ok;
  }

  bool fail(MDNSResult r) {
    lastResult_ = r;
    return false;
  }

  UDP *udp_ = nullptr;
  char fqdn_[TINYMDNS_NAME_MAX] = {0};
  IPAddress address_;
  IPAddress subnetMask_;  // 0.0.0.0 = accept answers from any network
  uint32_t ttl_ = TINYMDNS_DEFAULT_TTL;
  uint32_t timeoutMs_ = TINYMDNS_DEFAULT_TIMEOUT_MS;
  uint32_t announceIntervalMs_ = TINYMDNS_DEFAULT_ANNOUNCE_INTERVAL_MS;
  uint32_t lastAnnounce_ = 0;
  bool started_ = false;

  // resolve() state
  char queryName_[TINYMDNS_NAME_MAX] = {0};
  bool queryActive_ = false;
  bool queryAnswered_ = false;
  IPAddress answerIp_;
  uint32_t answerTtl_ = 0;
  uint32_t lastTtl_ = 0;
  MDNSResult lastResult_ = MDNSResult::NotConfigured;
};

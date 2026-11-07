// Host-side tests for TinymDNS. Build and run with:
//   g++ -std=c++17 -Itests/stubs -Isrc tests/test_main.cpp -o tinymdns_tests && ./tinymdns_tests
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "TinymDNS.h"

uint32_t g_mockMillis = 0;

struct Packet {
  IPAddress ip;
  uint16_t port;
  std::vector<uint8_t> data;
};

// In-memory UDP. Sent packets are recorded; received packets are queued in rx.
// onSend lets a test react to a sent packet (e.g. inject an answer).
class MockUDP : public UDP {
 public:
  std::vector<Packet> sent;
  std::deque<Packet> rx;
  std::function<void(MockUDP &, const Packet &)> onSend;

  int beginMulticast(IPAddress, uint16_t) override { return 1; }
  int begin(uint16_t) override { return 1; }
  void stop() override {}
  int beginPacket(IPAddress ip, uint16_t port) override {
    out_.ip = ip;
    out_.port = port;
    out_.data.clear();
    return 1;
  }
  size_t write(const uint8_t *buf, size_t size) override {
    out_.data.insert(out_.data.end(), buf, buf + size);
    return size;
  }
  int endPacket() override {
    sent.push_back(out_);
    if (onSend) onSend(*this, sent.back());
    return 1;
  }
  int parsePacket() override {
    if (rx.empty()) return 0;
    cur_ = rx.front();
    rx.pop_front();
    pos_ = 0;
    return (int)cur_.data.size();
  }
  int available() override { return (int)(cur_.data.size() - pos_); }
  int read() override { return pos_ < cur_.data.size() ? cur_.data[pos_++] : -1; }
  int read(uint8_t *buf, size_t len) override {
    size_t n = 0;
    while (n < len && pos_ < cur_.data.size()) buf[n++] = cur_.data[pos_++];
    return (int)n;
  }
  IPAddress remoteIP() override { return cur_.ip; }
  uint16_t remotePort() override { return cur_.port; }

 private:
  Packet out_, cur_;
  size_t pos_ = 0;
};

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      g_failures++;                                                          \
    }                                                                        \
  } while (0)

static std::vector<uint8_t> makeQuery(const char *fqdn, uint16_t id = 0) {
  uint8_t buf[512];
  size_t n = TinymDNSWire::buildQuery(buf, sizeof(buf), fqdn, TinymDNSWire::kTypeA);
  std::vector<uint8_t> v(buf, buf + n);
  v[0] = (uint8_t)(id >> 8);
  v[1] = (uint8_t)(id & 0xFF);
  return v;
}

static void testWire() {
  char fqdn[64];
  CHECK(TinymDNSWire::makeLocalName("esp32", fqdn, sizeof(fqdn)));
  CHECK(std::string(fqdn) == "esp32.local");
  CHECK(TinymDNSWire::makeLocalName("Printer.LOCAL", fqdn, sizeof(fqdn)));
  CHECK(std::string(fqdn) == "Printer.LOCAL");
  CHECK(!TinymDNSWire::makeLocalName("a.b", fqdn, sizeof(fqdn)));
  CHECK(!TinymDNSWire::makeLocalName("", fqdn, sizeof(fqdn)));
  CHECK(!TinymDNSWire::makeLocalName(".local", fqdn, sizeof(fqdn)));

  // Name encode / decode round trip.
  uint8_t enc[64];
  size_t n = TinymDNSWire::encodeName("esp32.local", enc, sizeof(enc));
  CHECK(n == 13);  // 1+5 "esp32", 1+5 "local", 1 root terminator
  char out[64];
  CHECK(TinymDNSWire::decodeName(enc, n, 0, out, sizeof(out)) == n);
  CHECK(std::string(out) == "esp32.local");

  // Case-insensitive comparison.
  CHECK(TinymDNSWire::equalsIgnoreCase("ESP32.local", "esp32.LOCAL"));
  CHECK(!TinymDNSWire::equalsIgnoreCase("esp32.local", "esp33.local"));
}

static void testResponder() {
  MockUDP udp;
  TinymDNS mdns;
  IPAddress self(192, 168, 1, 50);
  CHECK(mdns.begin(udp, "esp32", self));
  CHECK(mdns.isActive());
  CHECK(std::string(mdns.fqdn()) == "esp32.local");

  // begin() announces the registration.
  CHECK(udp.sent.size() == 1);
  IPAddress ip;
  uint32_t ttl = 0;
  CHECK(TinymDNSWire::parseAResponse(udp.sent[0].data.data(), udp.sent[0].data.size(),
                                     "esp32.local", ip, ttl));
  CHECK(ip == self);
  CHECK(ttl == TINYMDNS_DEFAULT_TTL);

  // Multicast query for our name -> multicast answer.
  udp.rx.push_back({IPAddress(192, 168, 1, 20), TINYMDNS_PORT, makeQuery("esp32.local")});
  mdns.loop();
  CHECK(udp.sent.size() == 2);
  CHECK(udp.sent[1].ip == TINYMDNS_MULTICAST_IP);
  CHECK(udp.sent[1].port == TINYMDNS_PORT);

  // Legacy unicast query (source port != 5353) -> unicast reply with same ID and question.
  udp.rx.push_back({IPAddress(192, 168, 1, 20), 40000, makeQuery("esp32.local", 0x1234)});
  mdns.loop();
  CHECK(udp.sent.size() == 3);
  CHECK(udp.sent[2].ip == IPAddress(192, 168, 1, 20));
  CHECK(udp.sent[2].port == 40000);
  CHECK(TinymDNSWire::readU16(udp.sent[2].data.data()) == 0x1234);
  CHECK(TinymDNSWire::parseAResponse(udp.sent[2].data.data(), udp.sent[2].data.size(),
                                     "esp32.local", ip, ttl));
  CHECK(ttl <= 10);

  // Query for an unrelated name -> no answer.
  udp.rx.push_back({IPAddress(192, 168, 1, 20), TINYMDNS_PORT, makeQuery("other.local")});
  mdns.loop();
  CHECK(udp.sent.size() == 3);

  // setAddress() re-announces the new address.
  mdns.setAddress(IPAddress(192, 168, 1, 99));
  CHECK(udp.sent.size() == 4);
  CHECK(TinymDNSWire::parseAResponse(udp.sent[3].data.data(), udp.sent[3].data.size(),
                                     "esp32.local", ip, ttl));
  CHECK(ip == IPAddress(192, 168, 1, 99));

  mdns.end();
  CHECK(!mdns.isActive());
}

static void testResolve() {
  // Answer a query for "printer.local" from inside onSend.
  MockUDP udp;
  TinymDNS mdns;
  mdns.begin(udp, "esp32", IPAddress(192, 168, 1, 50));
  udp.onSend = [](MockUDP &u, const Packet &p) {
    bool isQuery = p.data.size() > 2 && (p.data[2] & 0x80) == 0;
    if (!isQuery) return;
    uint8_t buf[512];
    size_t n = TinymDNSWire::buildAResponse(buf, sizeof(buf), 0, "printer.local",
                                            IPAddress(192, 168, 1, 77), 120, false);
    u.rx.push_back({IPAddress(192, 168, 1, 77), TINYMDNS_PORT, std::vector<uint8_t>(buf, buf + n)});
  };
  IPAddress ip;
  CHECK(mdns.resolve("printer", ip));
  CHECK(ip == IPAddress(192, 168, 1, 77));
  CHECK(mdns.lastTtl() == 120);
  CHECK(mdns.lastResult() == MDNSResult::Ok);

  // Timeout when nobody answers.
  MockUDP udp2;
  TinymDNS m2;
  m2.begin(udp2, "esp32", IPAddress(192, 168, 1, 50));
  m2.setTimeout(100);
  uint32_t t0 = g_mockMillis;
  CHECK(!m2.resolve("nobody", ip));
  CHECK(m2.lastResult() == MDNSResult::Timeout);
  CHECK(g_mockMillis - t0 >= 100);

  // Invalid names are rejected without sending.
  size_t before = udp2.sent.size();
  CHECK(!m2.resolve("a.b", ip));
  CHECK(m2.lastResult() == MDNSResult::InvalidName);
  CHECK(udp2.sent.size() == before);

  // Not started.
  TinymDNS m3;
  CHECK(!m3.resolve("printer", ip));
  CHECK(m3.lastResult() == MDNSResult::NotConfigured);
}

int main() {
  testWire();
  testResponder();
  testResolve();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("All TinymDNS tests passed\n");
  return 0;
}

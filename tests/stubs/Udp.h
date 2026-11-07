// Host-side stand-in for the Arduino UDP base class.
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

class UDP {
 public:
  virtual ~UDP() = default;
  virtual int beginMulticast(IPAddress ip, uint16_t port) = 0;
  virtual int begin(uint16_t port) = 0;
  virtual void stop() = 0;
  virtual int beginPacket(IPAddress ip, uint16_t port) = 0;
  virtual size_t write(const uint8_t *buf, size_t size) = 0;
  virtual int endPacket() = 0;
  virtual int parsePacket() = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int read(uint8_t *buf, size_t len) = 0;
  virtual IPAddress remoteIP() = 0;
  virtual uint16_t remotePort() = 0;
};

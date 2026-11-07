// Host-side stand-in for the Arduino IPAddress class.
#pragma once

#include <Arduino.h>

class IPAddress {
 public:
  IPAddress() : b_{0, 0, 0, 0} {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : b_{a, b, c, d} {}
  uint8_t operator[](int i) const { return b_[i]; }
  bool operator==(const IPAddress &o) const {
    for (int i = 0; i < 4; i++)
      if (b_[i] != o.b_[i]) return false;
    return true;
  }

 private:
  uint8_t b_[4];
};

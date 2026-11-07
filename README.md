# TinymDNS

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![Build: CMake](https://img.shields.io/badge/Build-CMake-064F8C.svg?logo=cmake)](CMakeLists.txt)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

TinymDNS is a header-only mDNS (RFC 6762) library for Arduino. A single object
registers a local hostname, answers `name.local` queries, announces itself on
the network, and resolves other `.local` names. It works with any Arduino
`UDP` implementation, such as `WiFiUDP` or `EthernetUDP`.

It provides:

- **[`TinymDNS`](https://pschatzmann.github.io/TinymDNS/class_tinym_d_n_s.html)**: registers a hostname and IPv4 address, answers queries, announces the address, and resolves `.local` names.

## Installation

### Arduino IDE

Download the library as a ZIP file and add it with *Sketch → Include Library →
Add .ZIP Library…*, or clone it into your Arduino libraries folder:

```bash
cd ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinymDNS.git
```

### PlatformIO

```ini
lib_deps = https://github.com/pschatzmann/TinymDNS.git
```

## Platforms

TinymDNS only needs a UDP object, so it works with any Arduino network stack. Pass the platform's UDP class to the constructor.

| Platform | Include | UDP class | Notes |
|----------|---------|-----------|-------|
| ESP32 (core 3.x) | `WiFi.h`, `WiFiUdp.h` | `WiFiUDP` | Use `WiFi.setSleep(false)` |
| ESP8266 (core 3.x) | `ESP8266WiFi.h`, `WiFiUdp.h` | `WiFiUDP` | Use `WiFi.setSleepMode(WIFI_NONE_SLEEP)`. The library calls the ESP8266 `beginMulticast(interface, group, port)` overload internally |
| UNO R4 WiFi | `WiFiS3.h`, `WiFiUdp.h` | `WiFiUDP` | Compiles. Not hardware tested |
| Pico W (RP2040) | `WiFi.h`, `WiFiUdp.h` | `WiFiUDP` | Compiles. Not hardware tested |
| Ethernet (W5x00) | `Ethernet.h`, `EthernetUdp.h` | `EthernetUDP` | The Ethernet library implements `beginMulticast()` (multicast mode on the chip), so this is expected to work. Not hardware tested |
| UNO R4 Ethernet | `Ethernet.h` | `EthernetUDP` | The core's lwIP is built without IGMP, so the group join is not effective. TinymDNS falls back to a plain UDP socket on port 5353 (`isMulticast()` returns false): announcements and queries are sent to 224.0.0.251, but receiving queries depends on the stack. Not hardware tested |

If joining the multicast group fails, `begin()` falls back to a plain UDP socket and only returns false if that also fails. Use `isMulticast()` to see which mode is active.

Include the platform headers before `TinymDNS.h`. The example `examples/MDNS/MDNS.ino` selects the right WiFi header and sleep call for ESP32 and ESP8266 automatically.

## Documentation

- [Wiki](https://github.com/pschatzmann/TinymDNS/wiki): usage, limitations, and testing.
- [Doxygen API reference](https://pschatzmann.github.io/TinymDNS/annotated.html).
- [`examples/`](examples/): `MDNS` sketch.

## License

Released under the MIT License. See [LICENSE](LICENSE).

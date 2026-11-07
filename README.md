# TinymDNS

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![Build: CMake](https://img.shields.io/badge/Build-CMake-064F8C.svg?logo=cmake)](CMakeLists.txt)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

TinymDNS is a header-only mDNS (RFC 6762) library for Arduino. A single object
registers a local hostname, answers `name.local` queries, announces itself on
the network, and resolves other `.local` names. It works with any Arduino
`UDP` implementation, such as `WiFiUDP` or `EthernetUDP`.

It provides:

- **[`TinymDNS`](https://pschatzmann.github.io/TinymDNS/class_tiny_m_d_n_s.html)**: registers a hostname and IPv4 address, answers queries, announces the address, and resolves `.local` names.
- **[`MDNSResult`](https://pschatzmann.github.io/TinymDNS/enum_m_d_n_s_result.html)**: the outcome of the most recent `resolve()` call.

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

## Documentation

- [Wiki](https://github.com/pschatzmann/TinymDNS/wiki): usage guides for
  responding, resolving, and testing.
- [Doxygen API reference](https://pschatzmann.github.io/TinymDNS/annotated.html).
- [`examples/`](examples/): `MDNS` sketch.

## Limitations

- IPv4 A records only.
- Hostname only: no DNS-SD service or TXT records.
- No conflict probing or renaming.
- `resolve()` is blocking.

## Testing

Host-side tests use in-memory stubs and need no hardware:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

Released under the MIT License. See [LICENSE](LICENSE).

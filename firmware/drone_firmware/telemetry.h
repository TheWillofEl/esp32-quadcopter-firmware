// Wi-Fi telemetry: the drone runs its own Wi-Fi Access Point (not joining
// a home router) and broadcasts attitude + battery data over UDP.
//
// Why an access point instead of joining a home Wi-Fi network: this is
// a flight controller, and it needs to work anywhere — a field, a gym,
// outdoors — not just in range of one specific router. An AP also gives
// a fixed, always-known IP address (192.168.4.1, the ESP32 Arduino core's
// default) with zero configuration on the drone side. This matches how
// the reference project (LiteWing) works too: it creates its own SSID
// that a phone or laptop connects to directly.
//
// Why UDP instead of TCP: telemetry is a live stream where a late packet
// is useless (by the time it arrives the drone's attitude has already
// changed), so there's nothing to gain from TCP's retransmission/ordering
// guarantees — they'd only add latency. UDP just fires each packet and
// moves on, which is the right trade for a real-time control link too
// (Phase 5).

#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>

class Telemetry {
 public:
  // Change these before flying somewhere with other people's drones
  // nearby, so SSIDs don't collide. Password must be 8+ characters for
  // WPA2 (ESP32 AP requirement).
  static constexpr const char *AP_SSID = "drone-firmware";
  static constexpr const char *AP_PASSWORD = "flyme1234";
  static const uint16_t TELEMETRY_PORT = 4210;

  void begin() {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("Wi-Fi AP started. SSID: %s  IP: %s\n", AP_SSID,
                   ip.toString().c_str());
    udp_.begin(TELEMETRY_PORT);
  }

  // Sends one telemetry packet to whichever address last sent us a
  // packet (so the ground station doesn't need a hardcoded drone IP
  // beyond connecting to the AP) — falls back to a broadcast if no
  // ground station has contacted us yet.
  void send(float roll_deg, float pitch_deg, float battery_volts,
            uint32_t loop_hz) {
    checkForGroundStation();

    // Simple, human-readable, comma-separated line. Easy to parse in
    // Python with zero dependencies, and easy to read directly in a
    // packet sniffer while debugging.
    char msg[96];
    int len = snprintf(msg, sizeof(msg), "roll=%.2f,pitch=%.2f,batt=%.2f,hz=%lu",
                        roll_deg, pitch_deg, battery_volts,
                        (unsigned long)loop_hz);

    IPAddress dest = has_ground_station_ ? ground_station_ip_
                                          : IPAddress(255, 255, 255, 255);
    udp_.beginPacket(dest, TELEMETRY_PORT);
    udp_.write((const uint8_t *)msg, len);
    udp_.endPacket();
  }

  // Phase 5: control_link.h reads pilot command packets off this same
  // socket/port rather than opening a second one — one UDP endpoint
  // handles both directions, same as how a single TCP connection can be
  // read and written. drone_firmware.ino calls this once per loop,
  // before Telemetry::send(), so a just-arrived command packet is
  // consumed by ControlLink and never seen by
  // Telemetry::checkForGroundStation() as a "hello."
  WiFiUDP &udp() { return udp_; }

  // Phase 5: called from drone_firmware.ino right after a ControlLink
  // packet arrives, so telemetry replies go straight to the pilot's
  // address from the very first real command packet — no separate
  // "hello" packet required once an app is actually sending commands
  // (checkForGroundStation()'s hello-based discovery still exists for
  // tools/ground_station.py, which only listens and never commands).
  void setGroundStation(IPAddress ip) {
    ground_station_ip_ = ip;
    has_ground_station_ = true;
  }

 private:
  WiFiUDP udp_;
  IPAddress ground_station_ip_;
  bool has_ground_station_ = false;

  // If the ground station has sent us anything (even a single byte to
  // say hello), remember its address so replies go directly to it
  // instead of a broadcast every packet. Phase 5: this only sees
  // "hello"-style packets that ControlLink::poll() didn't already
  // consume (ControlLink runs first each loop) — a real control packet
  // still teaches Telemetry the ground station's address on its first
  // arrival, since ControlLink itself doesn't touch ground_station_ip_.
  void checkForGroundStation() {
    int packet_size = udp_.parsePacket();
    if (packet_size > 0) {
      ground_station_ip_ = udp_.remoteIP();
      has_ground_station_ = true;
      uint8_t discard[32];
      udp_.read(discard, sizeof(discard));
    }
  }
};

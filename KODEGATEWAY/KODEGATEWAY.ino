// ================= GATEWAY CODE (Update ke Vercel HTTPS) =================

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// --- GANTI URL ke Vercel ---
const char* sensorDataUrl = "https://fire-detec.vercel.app/api/sensor-data";
const char* registerDeviceUrl = "https://fire-detec.vercel.app/api/register-device";

#define BELL_ID 1
#define RELAY_PIN 14
#define RELAY_ON  LOW
#define RELAY_OFF HIGH
uint8_t panelAddress[] = {0x08, 0xF9, 0xE0, 0x75, 0xDC, 0xC7}; // ganti sesuai kmac address panel //
const float SUHU_BATAS_ALARM = 35.0;

const long SILENCE_DURATION = 60000;
volatile bool alarmSilenced = false;
volatile unsigned long silenceStartTime = 0;

typedef struct struct_message { int id; float value; char device_name[20]; } struct_message;
struct_message myData;
typedef struct struct_message_laporan { int bell_id; int trigger_id; bool is_ringing; } struct_message_laporan;
struct_message_laporan laporanBalik;
typedef struct struct_channel_info { int channel; } struct_channel_info;
struct_channel_info channelData;
typedef struct struct_discovery_ping { int ping_id; } struct_discovery_ping;
struct_discovery_ping receivedPing;

volatile bool newDataToSend = false;
char macStrToSend[18];
int sensorIdToSend;
float valueToSend;

void OnReportSent(uint8_t *mac_addr, uint8_t status) {
  Serial.print("Status Laporan ke Panel: ");
  Serial.println(status == 0 ? "BERHASIL" : "GAGAL");
}

void registerNewDevice(const char* macAddress) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setInsecure();
    
    HTTPClient http;
    
    Serial.printf("Mencoba mendaftarkan MAC baru: %s ke %s\n", macAddress, registerDeviceUrl);
    Serial.printf("Free Heap before registration: %u\n", ESP.getFreeHeap());
    
    if (http.begin(*client, registerDeviceUrl)) {
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(10000);

        StaticJsonDocument<100> doc;
        doc["macAddress"] = macAddress;
        String requestBody;
        serializeJson(doc, requestBody);
        
        int httpResponseCode = http.POST(requestBody);
        
        if (httpResponseCode > 0) {
            Serial.printf("Respon registrasi: %d\n", httpResponseCode);
            String payload = http.getString();
            Serial.println(payload);
        } else {
            Serial.printf("Gagal mengirim permintaan registrasi, error: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        http.end();
    } else {
        Serial.println("Gagal memulai koneksi HTTP untuk registrasi.");
    }
}

void kirimKeWebsite(const char* macAddress, int sensorId, float value) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setInsecure();
    
    HTTPClient http;
    
    Serial.print("Mengirim ke Website: ");
    Serial.printf("Free Heap before sending data: %u\n", ESP.getFreeHeap());
    
    if (http.begin(*client, sensorDataUrl)) {
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(10000);

        const char* sensorTypeStr;
        switch (sensorId) {
            case 1: sensorTypeStr = "HEAT"; break;
            case 2: sensorTypeStr = "SMOKE"; break;
            case 3: sensorTypeStr = "BREAKING_GLASS"; break;
            default: sensorTypeStr = "UNKNOWN"; break;
        }
        StaticJsonDocument<200> doc;
        doc["macAddress"] = macAddress;
        doc["sensorType"] = sensorTypeStr;
        doc["value"] = value;
        String requestBody;
        serializeJson(doc, requestBody);
        Serial.println(requestBody);

        int httpResponseCode = http.POST(requestBody);
        
        if (httpResponseCode > 0) {
            Serial.printf("Website Response code: %d\n", httpResponseCode);
            // PERBAIKAN UTAMA DI SINI
            if (httpResponseCode == 404) {
                Serial.println("Device belum terdaftar. Menutup koneksi dan bersiap registrasi...");
                http.end(); // TUTUP KONEKSI PERTAMA SEPENUHNYA
                delay(200); // Beri jeda 200ms agar ESP bisa 'bernapas'
                registerNewDevice(macAddress); // BARU MULAI KONEKSI KEDUA
            } else {
                http.end(); // Jika bukan 404, tetap tutup koneksi
            }
        } else {
            Serial.printf("Gagal mengirim data sensor, error: %s\n", http.errorToString(httpResponseCode).c_str());
            http.end(); // Jika error, tetap tutup koneksi
        }
    } else {
        Serial.println("Gagal memulai koneksi HTTP untuk data sensor.");
    }
}

// ... (Sisa kode dari kirimLaporanKePanel sampai akhir tidak perlu diubah) ...
void kirimLaporanKePanel(int triggerId, bool statusNyala) {
  laporanBalik.bell_id = BELL_ID;
  laporanBalik.trigger_id = triggerId;
  laporanBalik.is_ringing = statusNyala;
  esp_now_send(panelAddress, (uint8_t *) &laporanBalik, sizeof(laporanBalik));
}

void OnDataRecv(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  if (len == sizeof(receivedPing)) {
    memcpy(&receivedPing, incomingData, sizeof(receivedPing));
    if (receivedPing.ping_id == 99) {
      char macStr[18];
      sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
      Serial.printf("Ping diterima dari sensor: %s. Menjawab...\n", macStr);
      channelData.channel = WiFi.channel();
      esp_now_add_peer(mac_addr, ESP_NOW_ROLE_SLAVE, WiFi.channel(), NULL, 0);
      esp_now_send(mac_addr, (uint8_t *) &channelData, sizeof(channelData));
      esp_now_del_peer(mac_addr);
    }
  }
  else if (len == sizeof(myData)) {
    memcpy(&myData, incomingData, sizeof(myData));
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.println("------------------------------------");
    Serial.printf("Sinyal data diterima dari Sensor MAC: %s\n", macStr);
    if (myData.id >= 1 && myData.id < 10) {
        strcpy(macStrToSend, macStr);
        sensorIdToSend = myData.id;
        valueToSend = myData.value;
        newDataToSend = true;
    }
    bool isAlarmEvent = (myData.id == 2) || 
                          (myData.id == 3) || 
                          (myData.id == 1 && myData.value > SUHU_BATAS_ALARM);
    if (isAlarmEvent && !alarmSilenced) {
      Serial.println("!!! KONDISI ALARM TERPENUHI! MENGAKTIFKAN RELAY !!!");
      digitalWrite(RELAY_PIN, RELAY_ON);
      kirimLaporanKePanel(myData.id, true);
    } 
    else if (myData.id == 10) {
      Serial.println("Perintah manual dari Panel Kontrol diterima.");
      if (myData.value == 1) {
        alarmSilenced = false;
        digitalWrite(RELAY_PIN, RELAY_ON); 
        kirimLaporanKePanel(10, true);
      } else {
        alarmSilenced = true;
        silenceStartTime = millis();
        Serial.printf("ALARM DIAM SEMENTARA selama %ld detik.\n", SILENCE_DURATION / 1000);
        digitalWrite(RELAY_PIN, RELAY_OFF); 
        kirimLaporanKePanel(10, false);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nMemulai Gateway...");
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  Serial.println("Mencoba menghubungkan WiFi...");
  if (!wm.autoConnect("Gateway-Setup-AP", "password123")) { 
    Serial.println("Gagal terhubung. Restart...");
    delay(3000);
    ESP.restart(); 
  }
  Serial.println("\nBerhasil terhubung ke WiFi!");
  Serial.print("MAC Address Gateway (untuk ESP-NOW): ");
  Serial.println(WiFi.macAddress());
  Serial.printf("Channel WiFi: %d\n", WiFi.channel());
  if (esp_now_init() != 0) { 
    Serial.println("Error initializing ESP-NOW");
    return; 
  }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnReportSent);
  esp_now_add_peer(panelAddress, ESP_NOW_ROLE_SLAVE, WiFi.channel(), NULL, 0);
  Serial.println("\nSetup Selesai. Gateway siap menerima data.");
}
 
void loop() {
  // PERBAIKAN: Menggunakan variabel yang benar (silenceStartTime)
  if (alarmSilenced && (millis() - silenceStartTime >= SILENCE_DURATION)) {
    alarmSilenced = false;
    Serial.println("Mode diam berakhir. Sistem kembali normal.");
  }
  if (newDataToSend) {
    Serial.println("Mengirim data ke website dari loop()...");
    kirimKeWebsite(macStrToSend, sensorIdToSend, valueToSend);
    newDataToSend = false;
  }
}

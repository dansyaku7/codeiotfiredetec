// ================= SENSOR TEMPLATE (ACTIVE DISCOVERY) =================
// Versi ini TIDAK MEMBUTUHKAN SSID & Password.
// Perbaikan:
// Menggunakan metode "tanya-jawab" (ping) untuk menemukan channel Gateway,
// yang lebih andal antara ESP32 dan ESP8266.

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

// --- SENSOR-SPECIFIC LIBRARIES ---
#include <DHT.h> 

// ================== PENGATURAN SENSOR (WAJIB UBAH!) ==================
// --- 1. PILIH TIPE SENSOR ---
// #define SENSOR_TYPE_SUHU
#define SENSOR_TYPE_ASAP
// #define SENSOR_TYPE_KACA

// --- 2. MAC ADDRESS GATEWAY/BELL TUJUAN ---
uint8_t gatewayAddress[] = {0x48, 0xE7, 0x29, 0x6D, 0x98, 0x59}; // GANTI MAC ADDRESS SESUAI BELL 1-6 // 48:E7:29:6D:98:59

// --- 3. PENGATURAN PIN & BATAS SENSOR ---
#if defined(SENSOR_TYPE_SUHU)
  #define DHTPIN 4
  #define DHTTYPE DHT22
  DHT dht(DHTPIN, DHTTYPE);
  const float SUHU_BATAS_KIRIM = 35.0;
  int SENSOR_ID = 1;
  const char* DEVICE_NAME = "Suhu Ruangan";
#elif defined(SENSOR_TYPE_ASAP)
  #define SMOKE_PIN 34
  const float RO_CLEAN_AIR = 26.3; 
  const float PPM_BATAS_KIRIM = 300.0;
  int SENSOR_ID = 2;
  const char* DEVICE_NAME = "Asap Ruangan";
#elif defined(SENSOR_TYPE_KACA)
  #define GLASS_PIN 4
  int SENSOR_ID = 3;
  const char* DEVICE_NAME = "Kaca Pecah";
#endif
// =====================================================================

#if defined(SENSOR_TYPE_ASAP)
float konversiKePPM(int sensorValue, float ro) {
    if (sensorValue <= 0 || sensorValue >= 4095) return 0;
    float VRL = (sensorValue / 4095.0) * 3.3; 
    float Rs = (3.3 - VRL) / VRL;
    float Rs_Ro_ratio = Rs / ro;
    if (Rs_Ro_ratio <= 0) return 0;
    return pow(10, ( (log10(Rs_Ro_ratio) - 0.7) / -0.35 ) + log10(200) );
}
#endif

// Struct untuk data sensor
typedef struct struct_message { int id; float value; char device_name[20]; } struct_message;
struct_message myData;

// Struct untuk menerima jawaban channel dari Gateway
typedef struct struct_channel_info { int channel; } struct_channel_info;
struct_channel_info channelData;

// Struct BARU untuk mengirim ping pencarian
typedef struct struct_discovery_ping { int ping_id = 99; } struct_discovery_ping;
struct_discovery_ping myPing;

volatile bool channelFound = false;
int gatewayChannel = 0;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Kita bisa biarkan ini kosong atau menambahkan log jika perlu
}

// Callback ini sekarang akan menerima JAWABAN dari Gateway
void OnChannelRecv(const esp_now_recv_info_t * info, const uint8_t *incomingData, int len) {
  if (len == sizeof(channelData)) {
    memcpy(&channelData, incomingData, sizeof(channelData));
    if (memcmp(info->src_addr, gatewayAddress, 6) == 0) {
      gatewayChannel = channelData.channel;
      channelFound = true;
      Serial.printf("\nGateway Menjawab! Channel ditemukan: %d\n", gatewayChannel);
    }
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) { return; }

  // Daftarkan callback untuk menerima JAWABAN
  esp_now_register_recv_cb(OnChannelRecv);
  
  // Daftarkan peer broadcast untuk mengirim PING
  esp_now_peer_info_t peerInfoBroadcast;
  memset(&peerInfoBroadcast, 0, sizeof(peerInfoBroadcast));
  uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(peerInfoBroadcast.peer_addr, broadcastAddr, 6);
  peerInfoBroadcast.encrypt = false;
  
  Serial.println("Mulai pencarian aktif Gateway...");
  for (int ch = 1; ch <= 13 && !channelFound; ch++) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    peerInfoBroadcast.channel = ch;
    if(esp_now_add_peer(&peerInfoBroadcast) != ESP_OK){
      Serial.printf("Gagal menambah peer broadcast di channel %d\n", ch);
      continue;
    }
    
    Serial.printf("Mengirim Ping di Channel %d...\n", ch);
    esp_now_send(broadcastAddr, (uint8_t *) &myPing, sizeof(myPing));
    esp_now_del_peer(broadcastAddr);
    delay(250); // Tunggu jawaban
  }

  if (!channelFound) {
    Serial.println("Gateway tidak ditemukan. Restart dalam 10 detik...");
    delay(10000);
    ESP.restart();
  }

  // Konfigurasi ulang ESP-NOW untuk mode pengiriman data normal
  esp_now_deinit();
  if (esp_now_init() != ESP_OK) { return; }

  esp_wifi_set_channel(gatewayChannel, WIFI_SECOND_CHAN_NONE);
  esp_now_register_send_cb(OnDataSent);
  
  esp_now_peer_info_t peerInfoGateway;
  memset(&peerInfoGateway, 0, sizeof(peerInfoGateway));
  memcpy(peerInfoGateway.peer_addr, gatewayAddress, 6);
  peerInfoGateway.channel = gatewayChannel;
  peerInfoGateway.encrypt = false;
  if (esp_now_add_peer(&peerInfoGateway) != ESP_OK){ return; }

  #if defined(SENSOR_TYPE_ASAP)
    pinMode(SMOKE_PIN, INPUT);
  #endif
  
  Serial.printf("Setup Sensor %s Selesai. Siap mengirim ke Gateway.\n", DEVICE_NAME);
  Serial.print("MAC Address Sensor ini: ");
  Serial.println(WiFi.macAddress());
}
 
void loop() {
  // Logika loop tidak berubah
  float sensorValue = 0;
  bool shouldSend = false;

  #if defined(SENSOR_TYPE_ASAP)
    int nilaiAnalog = analogRead(SMOKE_PIN);
    sensorValue = konversiKePPM(nilaiAnalog, RO_CLEAN_AIR);
    if (sensorValue > PPM_BATAS_KIRIM) {
      shouldSend = true;
    }
  #endif

  if (shouldSend) {
    Serial.printf("!!! KONDISI TERPENUHI! Mengirim data: %.2f\n", sensorValue);
    myData.id = SENSOR_ID;
    myData.value = sensorValue;
    strcpy(myData.device_name, DEVICE_NAME);
    
    esp_now_send(gatewayAddress, (uint8_t *) &myData, sizeof(myData));
    delay(30000); 
  }
  delay(2000);
}

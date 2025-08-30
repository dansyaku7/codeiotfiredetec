// ================= SENSOR SUHU (ACTIVE DISCOVERY) =================
// Versi ini TIDAK MEMBUTUHKAN SSID & Password.
// Mengirim data suhu setiap 15 detik.

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include <DHT.h> 

// ================== PENGATURAN SENSOR (WAJIB UBAH!) ==================
// --- MAC ADDRESS GATEWAY/BELL TUJUAN ---
uint8_t gatewayAddress[] = {0x48, 0xE7, 0x29, 0x6D, 0x98, 0x59}; // GANTI SAMA MAC ADDRESS SETIAP BELL / GATEWAY //

// --- PENGATURAN PIN & NAMA SENSOR ---
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
int SENSOR_ID = 1;
const char* DEVICE_NAME = "Suhu Ruangan";
// =====================================================================

// Struct untuk data sensor
typedef struct struct_message { int id; float value; char device_name[20]; } struct_message;
struct_message myData;

// Struct untuk menerima jawaban channel dari Gateway
typedef struct struct_channel_info { int channel; } struct_channel_info;
struct_channel_info channelData;

// Struct untuk mengirim ping pencarian
typedef struct struct_discovery_ping { int ping_id = 99; } struct_discovery_ping;
struct_discovery_ping myPing;

volatile bool channelFound = false;
int gatewayChannel = 0;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Status Pengiriman: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Berhasil" : "Gagal");
}

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

  esp_now_register_recv_cb(OnChannelRecv);
  
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
    delay(250);
  }

  if (!channelFound) {
    Serial.println("Gateway tidak ditemukan. Restart dalam 10 detik...");
    delay(10000);
    ESP.restart();
  }

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

  dht.begin();
  
  Serial.printf("Setup Sensor %s Selesai. Siap mengirim ke Gateway.\n", DEVICE_NAME);
  Serial.print("MAC Address Sensor ini: ");
  Serial.println(WiFi.macAddress());
}

// Variabel timer untuk pengiriman data suhu
unsigned long lastSendTime = 0;
const long sendInterval = 15000; // 15 detik
 
void loop() {
  unsigned long currentTime = millis();
  if (currentTime - lastSendTime >= sendInterval) {
    lastSendTime = currentTime;
    
    float sensorValue = dht.readTemperature();
    
    if (!isnan(sensorValue)) {
        Serial.printf("Suhu saat ini: %.2f *C. Mengirim data...\n", sensorValue);
        myData.id = SENSOR_ID;
        myData.value = sensorValue;
        strcpy(myData.device_name, DEVICE_NAME);
        esp_now_send(gatewayAddress, (uint8_t *) &myData, sizeof(myData));
    } else {
        Serial.println("Gagal membaca sensor DHT!");
    }
  }
}

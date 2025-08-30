// ================= SENSOR KACA PECAH (ESP8266 - ZERO CONFIG) =================
// Versi ini TIDAK MEMBUTUHKAN SSID & Password.
// Ia akan secara otomatis mencari channel Gateway dengan metode "tanya-jawab".
// UPDATE: Menambahkan pengiriman data inisialisasi setelah setup.

#include <ESP8266WiFi.h>
#include <espnow.h>

// ================== PENGATURAN SENSOR (WAJIB UBAH!) ==================
// --- 1. MAC ADDRESS GATEWAY/BELL TUJUAN ---
// Ganti dengan MAC Address Bell yang akan menerima data dari sensor ini
uint8_t gatewayAddress[] = {0x48, 0xE7, 0x29, 0x6D, 0x98, 0x59}; // GANTI MAC ADDRESS SESUAI GATEWAY //

// --- 2. PENGATURAN PIN & NAMA SENSOR ---
#define GLASS_PIN 4 // Pin D2 pada NodeMCU
int SENSOR_ID = 3;
const char* DEVICE_NAME = "Kaca Pecah Pintu A";
// =====================================================================

// Struct untuk mengirim data sensor
typedef struct struct_message {
    int id;
    float value;
    char device_name[20];
} struct_message;
struct_message myData;

// Struct untuk menerima jawaban channel dari Gateway
typedef struct struct_channel_info {
    int channel;
} struct_channel_info;
struct_channel_info channelData;

// Struct untuk mengirim ping pencarian
typedef struct struct_discovery_ping {
    int ping_id = 99;
} struct_discovery_ping;
struct_discovery_ping myPing;

volatile bool channelFound = false;
int gatewayChannel = 0;

// Variabel penanda untuk mencegah spam alarm
bool alarmSent = false;

// Callback untuk status pengiriman data
void OnDataSent(uint8_t *mac_addr, uint8_t status) {
  Serial.print("Status Pengiriman Data: ");
  Serial.println(status == 0 ? "Berhasil" : "Gagal");
}

// Callback untuk menerima JAWABAN dari Gateway
void OnChannelRecv(uint8_t *mac_addr, uint8_t *incomingData, uint8_t len) {
  if (len == sizeof(channelData)) {
    memcpy(&channelData, incomingData, sizeof(channelData));
    // Cek apakah jawaban berasal dari Gateway yang benar
    if (memcmp(mac_addr, gatewayAddress, 6) == 0) {
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

  if (esp_now_init() != 0) {
    Serial.println("Error inisialisasi ESP-NOW");
    return;
  }

  // Set peran sebagai COMBO agar bisa mengirim dan menerima
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnChannelRecv);
  
  // Daftarkan peer broadcast untuk mengirim PING
  uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_add_peer(broadcastAddr, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  
  Serial.println("Mulai pencarian aktif Gateway...");
  for (int ch = 1; ch <= 13 && !channelFound; ch++) {
    wifi_set_channel(ch); // Ganti channel WiFi secara manual
    
    Serial.printf("Mengirim Ping di Channel %d...\n", ch);
    esp_now_send(broadcastAddr, (uint8_t *) &myPing, sizeof(myPing));
    delay(250); // Tunggu jawaban
  }
  
  esp_now_del_peer(broadcastAddr); // Hapus peer broadcast

  if (!channelFound) {
    Serial.println("Gateway tidak ditemukan. Restart dalam 10 detik...");
    delay(10000);
    ESP.restart();
  }

  // Konfigurasi ulang ESP-NOW untuk mode pengiriman data normal
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(OnDataSent);
  
  // Daftarkan Gateway sebagai peer utama
  esp_now_add_peer(gatewayAddress, ESP_NOW_ROLE_SLAVE, gatewayChannel, NULL, 0);

  // Inisialisasi pin sensor
  pinMode(GLASS_PIN, INPUT_PULLUP);
  
  Serial.printf("Setup Sensor %s Selesai. Siap mengirim ke Gateway.\n", DEVICE_NAME);
  Serial.print("MAC Address Sensor ini: ");
  Serial.println(WiFi.macAddress());

  // --- TAMBAHAN BARU DI SINI ---
  // Kirim data pertama kali sebagai "salam perkenalan" untuk registrasi
  Serial.println("Mengirim data inisialisasi untuk registrasi...");
  myData.id = SENSOR_ID;
  myData.value = 0; // Kirim 0 untuk status "Normal"
  strcpy(myData.device_name, DEVICE_NAME);
  esp_now_send(gatewayAddress, (uint8_t *) &myData, sizeof(myData));
  // -----------------------------
}
 
void loop() {
  // Baca status sensor
  int sensorStatus = digitalRead(GLASS_PIN);

  // Jika tombol DILEPAS (HIGH karena PULLUP) dan alarm BELUM pernah dikirim
  if (sensorStatus == HIGH && !alarmSent) {
    Serial.println("!!! TOMBOL DILEPAS (ALARM)! Mengirim sinyal...");
    
    myData.id = SENSOR_ID;
    myData.value = 1; // Kirim angka 1 untuk menandakan aktif
    strcpy(myData.device_name, DEVICE_NAME);
    
    // Kirim data ke Gateway
    esp_now_send(gatewayAddress, (uint8_t *) &myData, sizeof(myData));
    
    // Set penanda agar tidak mengirim spam
    alarmSent = true;
  }
  // Jika tombol DITEKAN (LOW), reset penanda agar siap untuk alarm berikutnya
  else if (sensorStatus == LOW) {
    if (alarmSent) {
      Serial.println("Tombol ditekan (Normal). Sistem direset.");
      alarmSent = false;
    }
  }
  
  delay(200); // Jeda pembacaan normal
}

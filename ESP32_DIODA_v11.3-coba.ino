/**
 * TUNGYU THERMOFORMING HEATER PREDICTIVE MAINTENANCE - ESP32 FIRMWARE
 * Version: v11.3_LIGHTWEIGHT (FIXED LINKER ERROR)
 * -----------------------------------------------------------------
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =================================================================
// 1. KONFIGURASI WI-FI & FIREBASE
// =================================================================
const char* WIFI_SSID     = "TECNO POVA 4";
const char* WIFI_PASSWORD = "joemada2";  

const String FIREBASE_URL = "https://ctfh-f0c6d-default-rtdb.firebaseio.com";
const String FIREBASE_API = "AIzaSyBK41jJSMb0u4SnaibRqRA1gelzjh40zIo";

// =================================================================
// 2. KONFIGURASI SENSOR & VARIABEL GLOBAL MULTIPLIER
// =================================================================
const bool DEMO_MODE = false;
const double SENSOR_RATIO     = 30; // 30A / 1V (rasio SCT-013-030)
const int TOTAL_SENSOR = 6;
const int SENSOR_PINS[TOTAL_SENSOR] = {32, 33, 34, 35, 36, 39};

const String sensorIDs[TOTAL_SENSOR]  = {"ct1","ct2","ct3","ct4","ct5","ct6"};
const String zonaNames[TOTAL_SENSOR]  = {
  "Upper Mold RS", "Upper Mold ST", "Upper Mold TR",
  "Lower Mold RS", "Lower Mold ST", "Lower Mold TR"
};

// Variabel Penampung Nilai Multiplier Jarak Jauh (Default Awal)
double remoteMultiplier[TOTAL_SENSOR] = {2.681, 2.480, 3.013, 3.171, 3.199, 2.989};

// Manajemen Waktu Siklus (Pembacaan & Logging)
unsigned long lastExecutionTime = 0;
const unsigned long executionInterval = 3000; // Pembacaan data sensor tiap 3 detik

unsigned long lastLogTime = 0;
const unsigned long logInterval = 60000; // Kirim data ke riwayat_heater tiap 1 menit

unsigned long lastFetchMultiplierTime = 0;
const unsigned long fetchMultiplierInterval = 10000; // Sinkronisasi otomatis kalibrasi dari cloud tiap 10 detik

// Deklarasi Prototip Fungsi (Harus Sama Persis dengan yang di Bawah)
double bacaIRMSInternalADC(int pin, int sensorIndex, bool showDebug);
double bacaIRMS_Demo(int sensorIndex);
void kirimKeFirebase(double arusHasil[]);
void kirimLogKeRiwayat(double ct1, double ct2, double ct3, double ct4, double ct5, double ct6);
void ambilMultiplierDariFirebase();
void connectWiFi();

// =================================================================
// 3. SETUP
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(1000); 

  Serial.println(F("\n\n=================================================="));
  Serial.println(F("   TUNGYU HEATER PREDICTIVE MAINTENANCE            "));
  Serial.println(F("   Firmware v11.3 - Lightweight REST Architecture  "));
  Serial.println(F("=================================================="));

  connectWiFi();
  analogReadResolution(12);

  // Mengambil nilai awal pengali kalibrasi langsung saat startup
  ambilMultiplierDariFirebase();

  Serial.println("[STATUS] Setup selesai. Memulai pemantauan...\n");
}

// =================================================================
// 4. LOOP UTAMA
// =================================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  
  unsigned long currentMillis = millis();

  // Sinkronisasi data Multiplier Kalibrasi secara berkala
  if (currentMillis - lastFetchMultiplierTime >= fetchMultiplierInterval) {
    lastFetchMultiplierTime = currentMillis;
    ambilMultiplierDariFirebase();
  }

  // Siklus Pembacaan Arus Sensor
  if (lastExecutionTime != 0 && (currentMillis - lastExecutionTime < executionInterval)) {
    return;
  }
  lastExecutionTime = currentMillis;

  Serial.println("\n--- Memulai Siklus Pembacaan & Pengiriman Data ---");
  double arus[TOTAL_SENSOR];

  // Membaca seluruh data fisik sensor CT
  for (int i = 0; i < TOTAL_SENSOR; i++) {
    if (DEMO_MODE) {
      arus[i] = bacaIRMS_Demo(i);
    } else {
      arus[i] = bacaIRMSInternalADC(SENSOR_PINS[i], i, true);
    }
    
    Serial.println("[MONITOR] " + sensorIDs[i] + " [" + zonaNames[i] + "]: " + String(arus[i], 3) + " A");
    delay(50); 
  }
  
  // Mengirim data pemantauan langsung ke Firebase Realtime Database
  kirimKeFirebase(arus);

  // Proses Auto-Logging berkala untuk Tabel Riwayat (Setiap 1 Menit)
  if (currentMillis - lastLogTime >= logInterval) {
    lastLogTime = currentMillis;
    Serial.println("[LOG SHEET] Memulai proses auto-logging berkala dari ESP32...");
    kirimLogKeRiwayat(arus[0], arus[1], arus[2], arus[3], arus[4], arus[5]);
  }
  
  Serial.println("--- Siklus Selesai ---");
}

// =================================================================
// 5. FUNGSI PENGIRIMAN DATA LIVE (FIREBASE REST API)
// =================================================================
void kirimKeFirebase(double arusHasil[]) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = FIREBASE_URL + "/monitoring_heater.json?auth=" + FIREBASE_API;
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonPayload = "{";
    for (int i = 0; i < TOTAL_SENSOR; i++) {
      jsonPayload += "\"" + sensorIDs[i] + "\":{";
      jsonPayload += "\"zona\":\"" + zonaNames[i] + "\",";
      jsonPayload += "\"arus\":" + String(arusHasil[i], 3);
      jsonPayload += "}";
      if (i < TOTAL_SENSOR - 1) jsonPayload += ",";
    }
    jsonPayload += "}";
    
    int httpResponseCode = http.PATCH(jsonPayload);
    
    if (httpResponseCode > 0) {
      Serial.print("[FIREBASE REST] Sukses Kirim Data! Code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("[FIREBASE REST] Gagal Kirim Data, Error: ");
      Serial.println(http.errorToString(httpResponseCode).c_str());
    }
    http.end();
  } else {
    Serial.println("[FIREBASE REST] Gagal, Wi-Fi Terputus!");
  }
}

// =================================================================
// 6. FUNGSI DATA LOG BERKALA (REST API POST) - SINKRON DASHBOARD
// =================================================================
void kirimLogKeRiwayat(double ct1, double ct2, double ct3, double ct4, double ct5, double ct6) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Endpoint diarahkan ke riwayat_heater.json agar tersimpan permanen sebagai list/log berkala
    String url = FIREBASE_URL + "/riwayat_heater.json?auth=" + FIREBASE_API;
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    // Alokasi memori string yang aman untuk mencegah fragmentasi heap RAM ESP32
    String jsonLog;
    jsonLog.reserve(256); 
    
    jsonLog = "{";
    jsonLog += "\"upper_rs\":" + String(ct1, 3) + ",";
    jsonLog += "\"upper_st\":" + String(ct2, 3) + ",";
    jsonLog += "\"upper_tr\":" + String(ct3, 3) + ",";
    jsonLog += "\"lower_rs\":" + String(ct4, 3) + ",";
    jsonLog += "\"lower_st\":" + String(ct5, 3) + ",";
    jsonLog += "\"lower_tr\":" + String(ct6, 3) + ",";
    // Menggunakan Server Value Timestamp bawaan Firebase RTDB (".sv": "timestamp")
    jsonLog += "\"timestamp\":{\".sv\":\"timestamp\"}";
    jsonLog += "}";
    
    int httpResponseCode = http.POST(jsonLog);
    
    if (httpResponseCode > 0) {
      Serial.print("[LOG SHEET] Push data riwayat sukses, Code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("[LOG SHEET] Push data riwayat GAGAL, Error: ");
      Serial.println(http.errorToString(httpResponseCode).c_str());
    }
    
    // Selalu tutup koneksi HTTP untuk mencegah memory leak
    http.end(); 
  } else {
    Serial.println("[LOG SHEET] Gagal kirim log, WiFi tidak terkoneksi.");
  }
}

// =================================================================
// 7. SINKRONISASI PENGALI KALIBRASI SECARA BERKALA DARI CLOUD
// =================================================================
void ambilMultiplierDariFirebase() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = FIREBASE_URL + "/konfigurasi_kalibrasi.json?auth=" + FIREBASE_API;
    
    http.begin(url);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 200) {
      String payload = http.getString();
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        if (doc.containsKey("m_ct1")) remoteMultiplier[0] = doc["m_ct1"].as<double>();
        if (doc.containsKey("m_ct2")) remoteMultiplier[1] = doc["m_ct2"].as<double>();
        if (doc.containsKey("m_ct3")) remoteMultiplier[2] = doc["m_ct3"].as<double>();
        if (doc.containsKey("m_ct4")) remoteMultiplier[3] = doc["m_ct4"].as<double>();
        if (doc.containsKey("m_ct5")) remoteMultiplier[4] = doc["m_ct5"].as<double>();
        if (doc.containsKey("m_ct6")) remoteMultiplier[5] = doc["m_ct6"].as<double>();
        
        Serial.println("[SYNC CLOUD] Sinkronisasi Multiplier Kalibrasi OTA Berhasil!");
      }
    }
    http.end();
  }
}

// =================================================================
// 8. KONEKSI WI-FI
// =================================================================
void connectWiFi() {
  Serial.print("[WIFI] Menghubungkan ke ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 15) {
    delay(500);
    Serial.print(".");
    attempt++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Terhubung! IP Perangkat: " + WiFi.localIP().toString());
  }
}

// =================================================================
// 9. FUNGSI BACA ADC DENGAN MULTIPLIER LIVE MEMORI RAM
// =================================================================
double bacaIRMSInternalADC(int pin, int sensorIndex, bool showDebug) {
  uint32_t totalMVolt = 0;
  int sample = 40;
  for(int i = 0; i < sample; i++) {
    totalMVolt += analogReadMilliVolts(pin);
    delay(1);
  }
  double mVoltRaw = (double)totalMVolt / (double)sample;

  double lowCutoff = 15.0; 
  double voltSCT = 0.0;
  
  double currentMultiplier = remoteMultiplier[sensorIndex];
  if (mVoltRaw > lowCutoff) {
    double mVoltSinyal = mVoltRaw;
    voltSCT = (mVoltSinyal * currentMultiplier) / 1000.0;
  }

  double irms = voltSCT * SENSOR_RATIO;
  if (irms < 0.50) {
    irms = 0.0;
  }

  if (showDebug) {
    Serial.print("[DEBUG] Pin " + String(pin));
    Serial.print(" | Raw ADC: " + String(mVoltRaw, 0) + " mV");
    Serial.print(" | Live Multiplier: " + String(currentMultiplier, 3));
    Serial.print(" | Arus: " + String(irms, 3) + " A");
    Serial.println();
  }

  return irms;
}

// =================================================================
// 10. DATA DUMMY DEMO MODE
// =================================================================
double bacaIRMS_Demo(int sensorIndex) {
  double baseValues[6] = {10.94, 10.90, 10.95, 10.94, 10.89, 10.92};
  return baseValues[sensorIndex] + ((double)random(-10, 10) / 100.0);
}
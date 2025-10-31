//ANOD

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include <driver/i2s.h>
#include "adpcm.h"
#include <ArduinoJson.h>
#include <HTTPUpdate.h>


String currentRecordingFile=""; 

// ---------------------- PIN DEFINITIONS ----------------------
#define I2S_WS    15
#define I2S_SCK   4
#define I2S_SD    5
#define SD_CS     21
#define SPI_MISO  22
#define SPI_MOSI  23
#define SPI_SCK   19
#define BLUE_LED  32
#define LED_PINg  33
#define LED_PINr  25
#define RESET_PIN 18

// ---------------------- CONFIG ----------------------
#define SAMPLE_RATE     16000
#define I2S_NUM         I2S_NUM_0
#define CHUNK_SAMPLES   1024
#define ADPCM_RATIO     2
#define FILE_LIMIT      (5 * 1024 * 1024)  // 5MB
#define VAD_THRESHOLD   100
#define ENERGY_DELETE_THRESHOLD 50
#define SILENCE_TIMEOUT 2000              // ms
#define MIN_VOICE_DURATION 1500           // ms

// ---------------------- GLOBALS ----------------------
WiFiManager wm;
File audioFile;
 static char fileToUpload[64];
int16_t predsample = 0;
int16_t adpcm_index = 0;
uint32_t lastVoiceTime = 0;
bool recording = false;
QueueHandle_t uploadQueue;
uint32_t voiceActiveTime = 0;
String currentFilename;
uint32_t totalEnergy = 0;
uint32_t totalSamples = 0;
uint32_t count = 10;
//-------------------------------------------------------------------------------------------------------------------------
String shop_name = "HILL";// CHANGE SHOP NAME HERE
//---------------------------------------------------------------------------------------------------------------------------

String S3_UPLOAD_BASE_URL = "    "; //  Here upload your aws url.

// ---------------------- UTILITIES ----------------------
void checkResetButton() {
    if (digitalRead(RESET_PIN) == LOW) {
      Serial.println("[INFO] Reset button pressed!");
      delay(3000); // Wait to see if it's a long press

      if(digitalRead(RESET_PIN) == LOW) {
        Serial.println("[INFO] Resetting WiFi...");
        wm.resetSettings();
        ESP.restart();
      } else {
        Serial.println("[INFO] Short press detected, ignoring.");
      }

      // Wait for button to be released before checking again
      while (digitalRead(RESET_PIN) == LOW) {
        delay(10); // Debounce wait
      }

      delay(200); // Small additional debounce buffer
    }
}

void connectWiFi() {
    Serial.begin(115200);
  delay(1000);
  Serial.println("[BOOT] Starting Xpert WiFi Manager");

  pinMode(RESET_PIN, INPUT_PULLUP);

  pinMode(LED_PINg, OUTPUT);
  digitalWrite(LED_PINg,HIGH);
  pinMode(LED_PINr, OUTPUT);
  digitalWrite(LED_PINr, LOW);

  // Optional: Customize page appearance
  wm.setCustomHeadElement(R"====(
    <style>
      h1 { font-size: 1.8em; text-align: center; margin: 20px 0; }
      button { background-color: orange !important; color: white !important; }
      button:hover { background-color: darkorange !important; }
    </style>
    <h1></h1>
  )====");

  wm.setTitle("Xpert WiFi Manager");

  // Start WiFi configuration portal
  while (!wm.autoConnect("Xpert Voice Pulse")) {
    checkResetButton();
  }

  Serial.println("[OK] WiFi connected!");
  digitalWrite(LED_PINr, HIGH);
  delay(100);
  digitalWrite(LED_PINg, LOW);
  delay(5000);
  digitalWrite(LED_PINg, HIGH);


}


void syncTime() {
     configTime(19800, 0, "time.google.com", "pool.ntp.org", "time.nist.gov");

     Serial.println("[INFO] Waiting for NTP time...");
     struct tm timeinfo;
     int retries = 0;
     while (!getLocalTime(&timeinfo) && retries < 20) {
       Serial.println("[INFO] Waiting for NTP time...");
       delay(1000);
       retries++;
     }

     if (getLocalTime(&timeinfo)) {
       Serial.println("[INFO] Time synced: " + String(asctime(&timeinfo)));
     } else {
       Serial.println("[ERROR] Time sync failed, fallback to timestamp offset");
     }
}


void initI2S() {
     i2s_config_t config = {
       .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
       .sample_rate = SAMPLE_RATE,
       .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
       .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
       .communication_format = I2S_COMM_FORMAT_I2S_MSB,
       .intr_alloc_flags = 0,
       .dma_buf_count = 4,
       .dma_buf_len = 1024,
       .use_apll = false,
       .tx_desc_auto_clear = false,
       .fixed_mclk = 0
  };

     i2s_pin_config_t pins = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = -1,
       .data_in_num = I2S_SD
  };

     i2s_driver_install(I2S_NUM, &config, 0, NULL);
     i2s_set_pin(I2S_NUM, &pins);
}


bool initSDCard() {
     SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
     if (!SD.begin(SD_CS)) {
       Serial.println("[ERROR] SD Card Mount Failed");
        return false;
     }
     Serial.println("[INFO] SD Card initialized.");
     return true;
}

String getTimestampFilename() {
     struct tm timeinfo;
     if (!getLocalTime(&timeinfo)) {
       uint32_t fallback = millis() / 1000;
       return "/record_" + String(fallback) + ".adpcm";
  }

     char filename[32];
      snprintf(filename, sizeof(filename), "/record_%04d%02d%02d_%02d%02d%02d.adpcm",
              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      return String(filename);
}

bool openNewFile() {
  String filename = getTimestampFilename();

  // Store the full path for consistency in comparisons
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }
  currentFilename = filename;
  currentRecordingFile = filename;  // Set the current recording file name

  audioFile = SD.open(filename, FILE_WRITE);
  if (!audioFile) {
    Serial.println("[ERROR] Failed to open file: " + filename);
    currentRecordingFile = "";  // Clear current recording reference
    return false;
  }

  Serial.println("[INFO] Recording to: " + filename);
  return true;
}

void finalizeRecordingAndValidate() {
  recording = false;
  digitalWrite(BLUE_LED, HIGH);
  audioFile.close();

  Serial.println("[DEBUG] Voice active time: " + String(voiceActiveTime) + " ms");

 if (voiceActiveTime < MIN_VOICE_DURATION) {
  Serial.println("[INFO] Voice too short. Deleting file.");
  SD.remove(currentFilename);
  currentRecordingFile = "";  // ⬅️ Add this
  return;
 }


  float avgEnergy = (float)totalEnergy / totalSamples;
  Serial.println("[INFO] Avg energy for full file: " + String(avgEnergy));

  if (avgEnergy < ENERGY_DELETE_THRESHOLD) {
  Serial.println("[INFO] Low energy. Deleting file.");
  SD.remove(currentFilename);
  currentRecordingFile = "";  // ⬅️ Add this
  return;
 }
 else {
    Serial.println("[INFO] File valid. Adding to upload queue.");
    strncpy(fileToUpload, currentFilename.c_str(), sizeof(fileToUpload));
    xQueueSend(uploadQueue, &fileToUpload, 0);
  }
}




bool detectVoice(int16_t *buffer, size_t len, uint32_t &avgEnergyOut) {
  uint32_t energy = 0;
  for (size_t i = 0; i < len; i++) {
    energy += abs(buffer[i]);
  }
  uint32_t avg = energy / len;
  avgEnergyOut = avg;
  checkResetButton();
  if (avgEnergyOut>=100){
   Serial.println("Avg energy: " + String(avg));}
  return avg > VAD_THRESHOLD;
  
}

// ---------------------- S3 UPLOAD TASK ----------------------
class SDFileStream : public Stream {
  File &file;
public:
  SDFileStream(File &f) : file(f) {}

  int available() override { return file.available(); }
  int read() override { return file.read(); }
  int peek() override { return file.peek(); }
  void flush() override { file.flush(); }
  size_t write(uint8_t) override { return 0; }  // not needed
 };


String cleanFileName(const String& filename) {
  if (filename.startsWith("/")) {
    return filename.substring(1);
  }
  return filename;
}


void uploadTask(void *parameter) {
  Serial.println("[TASK] Upload task started");
  while (true) {
    File root = SD.open("/");
    File file = root.openNextFile();

    while (file) {
      if (!file.isDirectory()) {
        String filename = file.name();
        if (!filename.startsWith("/")) filename = "/" + filename;

        // ✅ Skip the currently recording file
        if (filename == currentRecordingFile) {
          size_t fileSize = file.size();
          Serial.println("[INFO] Skipping currently recording file: " + filename + " (" + String(fileSize) + " bytes)");
          file.close();
          file = root.openNextFile();
          continue;
        }

        // ✅ If file was deleted before upload task reached it
        if (!SD.exists(filename)) {
          Serial.println("[INFO] File no longer exists. Skipping: " + filename);
          file.close();
          file = root.openNextFile();
          continue;
        }

        if (filename.endsWith(".adpcm")) {
          size_t fileSize = file.size();
          if (fileSize == 0) {
            file.close();
            file = root.openNextFile();
            continue;
          }

          Serial.println("[INFO] Uploading: " + filename + " (" + String(fileSize) + " bytes)");

          SDFileStream stream(file);  // use file stream

          // Prepare S3 URL
          String cleanName = cleanFileName(filename);

          String url = S3_UPLOAD_BASE_URL + cleanName;

          HTTPClient http;
          http.begin(url);
          http.addHeader("Content-Type", "binary/octet-stream");

          int httpCode = http.sendRequest("PUT", &stream, fileSize);

          file.flush();
          file.close();
          http.end();

          Serial.printf("[INFO] HTTP Response: %d\n", httpCode);

          // ✅ Safely delete after upload success
          if (httpCode == 200 || httpCode == 201) {
            String deleteName = cleanFileName(filename);
            notifyXpertServer(deleteName);

            if (SD.remove("/" + deleteName)) {
              Serial.println("[INFO] Deleted after upload: " + deleteName);
            } else {
              Serial.println("[ERROR] Failed to delete: " + deleteName);
            }
          } else {
            Serial.println("[WARN] Upload failed: " + filename);
          }
        } else {
          file.close(); // Non-audio file
        }
      } else {
        file.close(); // Directory
      }

      file = root.openNextFile();
    }

    Serial.println("[INFO] Upload task complete. Waiting 5 sec...");
    delay(5000);  // Shortened wait (you had 60s, reduced for faster retry)
  }
}


//--- MAIN SETUP ----------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(LED_PINg, OUTPUT);
  pinMode(LED_PINr, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(BLUE_LED, HIGH);

  connectWiFi();
  syncTime();
  //checkForOTAUpdate();  // 🔥 Check for OTA here
  if (!initSDCard()) while (1);
  initI2S();

  uploadQueue = xQueueCreate(4, sizeof(char[64]));
  xTaskCreatePinnedToCore(uploadTask, "UploadTask", 8192, NULL, 1, NULL, 1);

  Serial.println("[INFO] VAD + Upload system ready.");
}

// -------------------- Loop (VAD + ADPCM + SD + Queue) --------------------

void loop() {
  int16_t pcmBuffer[CHUNK_SAMPLES];
  uint8_t adpcmBuffer[CHUNK_SAMPLES / ADPCM_RATIO];
  size_t bytesRead;

  i2s_read(I2S_NUM, pcmBuffer, sizeof(pcmBuffer), &bytesRead, portMAX_DELAY);
  if (bytesRead != sizeof(pcmBuffer)) return;

  uint32_t chunkEnergy = 0;
  bool voiceDetected = detectVoice(pcmBuffer, CHUNK_SAMPLES, chunkEnergy);
  totalEnergy += chunkEnergy * CHUNK_SAMPLES;
  totalSamples += CHUNK_SAMPLES;

  uint32_t now = millis();

  if (voiceDetected) {
    if (!recording) {
      Serial.println("[INFO] Voice detected. Recording started.");
      recording = true;
      voiceActiveTime = 0;
      totalEnergy = 0;
      totalSamples = 0;
      predsample = 0;
      adpcm_index = 0;
      if (!openNewFile()) return;
    }
    lastVoiceTime = now;
    voiceActiveTime += (CHUNK_SAMPLES * 1000 / SAMPLE_RATE);
  }

  if (recording) {
    if(count>0){
    digitalWrite(BLUE_LED, LOW);
   
      
    }
    int encoded = adpcm_encode(pcmBuffer, adpcmBuffer, CHUNK_SAMPLES, &predsample, &adpcm_index);
    if (audioFile.write(adpcmBuffer, encoded) != encoded) {
      Serial.println("[ERROR] Write failed");
      audioFile.close();
      recording = false;
      return;
    }
    audioFile.flush();

    if (audioFile.size() >= FILE_LIMIT) {
      Serial.println("[INFO] File full (5 MB). Finalizing...");
      finalizeRecordingAndValidate();
    }

    // ⬇️ Calculate dynamic silence timeout based on how long voice has been active
 uint32_t currentSilenceTimeout = (voiceActiveTime > 4000) ? 5000 : 2000;

 if (now - lastVoiceTime > currentSilenceTimeout) {
  if (count != 0) {
    count--;
    Serial.print("COUNT: ");
    Serial.println(count);
  }
  Serial.println("[INFO] Silence timeout. Finalizing...");
  finalizeRecordingAndValidate();
 }

  } else {
    digitalWrite(BLUE_LED, HIGH);
  }
}





void notifyXpertServer(const String& audioFilename) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERROR] WiFi disconnected before HTTP POST! Reconnecting...");
    wm.autoConnect("Xpert Voice Pulse");
  }

  Serial.printf("[DEBUG] Free Heap: %d bytes\n", ESP.getFreeHeap());

  HTTPClient http;
  http.begin("   ");// server url
  
  http.addHeader("Content-Type", "application/json");

  String audioUrl = S3_UPLOAD_BASE_URL + audioFilename;

  DynamicJsonDocument jsonDoc(2048);
  jsonDoc["api_token"] = "  ";// enter shop token
  jsonDoc["api_for"] = "hardware_audio";

  JsonObject rawData = jsonDoc.createNestedObject("raw_data");
  rawData["audio_url"] = audioUrl;
  rawData["image_url"] = "";
  rawData["phone_number"] = "";
  rawData["shop_name"] = shop_name;
  rawData["shop_token"] = "   ";// enter shop token
  rawData["datatype"] = "audio";

  String jsonPayload;
  serializeJson(jsonDoc, jsonPayload);
  Serial.println("[INFO] Sending JSON to Xpert: " + jsonPayload);

  int httpCode = http.POST(jsonPayload);
  String response = http.getString();
  http.end();

  Serial.printf("[INFO] Xpert Server Response Code: %d\n", httpCode);
  Serial.println(response);
}





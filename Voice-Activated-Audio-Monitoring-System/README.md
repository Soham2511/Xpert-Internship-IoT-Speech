# Voice-Activated Audio Monitoring System
![Project Preview](project_preview.png)
This project is part of an internship advertisement solution developed for smart store environments. It is designed to automatically capture customer feedback at key store locations (like billing counters or product demo areas), where interactions are frequent.

The system uses an **ESP32 microcontroller** with an analog microphone to **detect and record only when voice activity is present**. The recorded audio is **uploaded to an AWS S3 bucket**, and transcription is performed server-side using **Deepgram Speech-to-Text API**. Transcriptions are then used to derive meaningful customer feedback, enabling stores to respond intelligently and improve service.

---

## 📌 Use Case: Real-Time Store Feedback Collection

Retail chains often struggle to collect honest, spontaneous customer feedback. This system:
- Listens **only when someone speaks**, reducing noise, storage, and privacy concerns.
- Uses **voice activity detection (VAD)** to avoid idle recordings.
- Sends audio to the cloud for **automatic transcription**.
- Allows stores to analyze feedback **without manual effort**.

---

## 🚀 Features

- 🔊 **Voice Detection:** Records only when voice is present using energy-based VAD.
- 💾 **Onboard Recording:** Uses SD card to temporarily store `.adpcm` compressed audio.
- 📶 **Wi-Fi Connectivity:** Automatically connects to Wi-Fi using a captive portal.
- ☁️ **S3 Upload:** Uploads recorded audio files to a configured AWS S3 bucket.
- 🧠 **Transcription Ready:** The server uses Deepgram to convert voice to text.
- 🔄 **Non-blocking Upload:** Upload happens in a **separate FreeRTOS task**, so recording is **never interrupted**.
- 🎇 **LED Status Feedback:** RGB LED shows system states (e.g., recording, Wi-Fi connected).
- 🔘 **Reset Button:** Allows Wi-Fi settings reset by long pressing a hardware button.

---

## 🧩 Architecture

[Customer Speaking]
↓
[Mic + ESP32]
↓ (ADPCM encoded)
[SD Card - local buffer]
↓
[Wi-Fi Upload to AWS S3]
↓
[Server Side: Deepgram API]
↓
[Transcribed Feedback for Store]


---

## 🔄 Multi-Threaded Upload (Key Design Point)

One major design consideration was ensuring **recording is never interrupted** during file uploads. To achieve this:

- The **main loop** handles:
  - I2S audio read
  - Voice detection
  - ADPCM compression
  - Writing to SD card

- A separate **FreeRTOS task (`uploadTask`)**:
  - Scans SD card for `.adpcm` files
  - Skips the currently recording file
  - Uploads completed files via HTTP PUT to S3
  - Notifies server via POST request with metadata

This architecture **separates time-sensitive audio handling from network delays**, ensuring robust, uninterrupted performance.

---

## 🛠️ Hardware Used

- ESP32-WROOM Dev Module
- Analog Microphone (e.g., MAX4466 / MAX9814)
- MicroSD Card Module (SPI)
- RGB LED (Optional for status indication)
- Pushbutton (for Wi-Fi reset)

---

## 📦 Software Components

- **Arduino framework**
- `WiFiManager` – dynamic Wi-Fi setup
- `HTTPClient` – for S3 upload and server notifications
- `SD.h` – file handling on SD card
- `I2S` – audio input
- `FreeRTOS` – multitasking (upload runs on core 1)

---

## ⚙️ Setup

1. **Hardware Connections:**
   - I2S Mic → ESP32 (WS, SCK, SD)
   - SD Card → SPI Pins + CS
   - LED → GPIO pins (optional)
   - Button → GPIO18 (active low)

2. **Firmware Configuration:**
   Edit the following variables in the code:
   ```cpp
   String S3_UPLOAD_BASE_URL = "";  // S3 PUT URL prefix
   const char* shop_name = "HILL";  // Store location name
   jsonDoc["api_token"] = "";       // Your API token
   rawData["shop_token"] = "";      // Your shop token
   http.begin("");                  // Server endpoint

3. Flashing the Firmware:

Install the required libraries (WiFiManager, ArduinoJson, etc.)

Select the ESP32 board and correct port

Upload code via Arduino IDE

🧠 Server-Side Integration
Once the file is uploaded to AWS S3, the ESP32 sends a POST request to your backend server. The server then uses Deepgram API to transcribe the audio into text:

Example Deepgram Request:
POST https://api.deepgram.com/v1/listen
Authorization: Token YOUR_DEEPGRAM_API_KEY
{
  "url": "https://your-bucket.s3.amazonaws.com/audio123.adpcm"
}
MIT License
© 2025 Avishek Mandal



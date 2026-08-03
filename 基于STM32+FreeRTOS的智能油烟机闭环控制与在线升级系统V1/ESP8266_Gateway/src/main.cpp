#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiClientSecureBearSSL.h>
#include <bearssl/bearssl_hash.h>
#include <time.h>

#if __has_include("local_config.h")
#include "local_config.h"
#else
#define OTA_WIFI_SSID ""
#define OTA_WIFI_PASSWORD ""
#define OTA_MANIFEST_URL ""
#define OTA_ROOT_CA ""
#define OTA_CONFIGURATION_READY 0
#endif

#define OTA_SOF0                  0xA5U
#define OTA_SOF1                  0x5AU
#define OTA_PROTOCOL_VERSION      1U
#define OTA_MAX_PAYLOAD           1024U
#define OTA_DATA_CHUNK_SIZE       256U
#define OTA_GATEWAY_BUILD_ID      "2026-07-31-r4"
#define OTA_APP_MAX_SIZE          0x00037000UL
#define OTA_TARGET                "hood-stm32f103rc-v1"
#define OTA_FIRMWARE_FILE         "/stm32.bin"
#define OTA_TEMP_FILE             "/fw.tmp"
#define OTA_MANIFEST_FILE         "/manifest.json"
#define OTA_CHECK_PERIOD_MS       (30UL * 60UL * 1000UL)
#define OTA_PREPARE_PERIOD_MS     5000UL
#define OTA_ACK_TIMEOUT_MS        1000UL
#define OTA_MAX_RETRIES           5U
#define OTA_DOWNLOAD_IDLE_TIMEOUT_MS 15000UL
#define OTA_TLS_DOWNLOAD_RX_SIZE  16384
#define OTA_TLS_DOWNLOAD_TX_SIZE  512
#define OTA_HTTP_MAX_ATTEMPTS      3U

/*
 * ESP8266 Serial/UART0 is reserved for the binary STM32 OTA protocol.
 * Serial1 is TX-only on GPIO2 and is safe for human-readable diagnostics.
 */
#define OTA_DEBUG_BAUD            115200U
#define OTA_DEBUG_PRINTF(...)     Serial1.printf(__VA_ARGS__)
#define OTA_DEBUG_PRINTLN(text)   Serial1.println(text)

enum MessageType : uint8_t {
  MSG_BL_HELLO = 0x01, MSG_BEGIN = 0x02, MSG_DATA = 0x03, MSG_END = 0x04,
  MSG_ACK = 0x05, MSG_NACK = 0x06, MSG_RESULT = 0x07, MSG_BL_TRACE = 0x08,
  MSG_OTA_PREPARE = 0x20, MSG_OTA_ACCEPT = 0x21, MSG_OTA_BUSY = 0x22,
  MSG_OTA_CHECK = 0x23, MSG_APP_BOOT_OK = 0x24
};

struct Manifest {
  uint32_t versionCode = 0;
  uint32_t size = 0;
  String version;
  String sha256;
  String url;
  uint8_t hash[32] = {0};
  uint8_t signature[64] = {0};
  bool valid = false;
};

struct ReceivedFrame {
  volatile bool ready = false;
  uint8_t type = 0;
  uint32_t sequence = 0;
  uint32_t offset = 0;
  uint16_t length = 0;
  uint8_t payload[32] = {0};
};

class FrameParser {
 public:
  void feed(uint8_t byte) {
    if (state_ == 0) { if (byte == OTA_SOF0) state_ = 1; return; }
    if (state_ == 1) {
      if (byte == OTA_SOF1) { state_ = 2; index_ = 0; expected_ = 12; }
      else state_ = (byte == OTA_SOF0) ? 1 : 0;
      return;
    }
    if (index_ >= sizeof(body_)) { reset(); return; }
    body_[index_++] = byte;
    if (index_ == 12) {
      uint16_t len = readLe16(body_ + 10);
      if (body_[0] != OTA_PROTOCOL_VERSION || len > OTA_MAX_PAYLOAD) { reset(); return; }
      expected_ = 12U + len + 4U;
    }
    if (index_ == expected_) {
      const uint16_t dataLen = expected_ - 4U;
      if (readLe32(body_ + dataLen) == crc32(body_, dataLen)) onFrame();
      reset();
    }
  }

 private:
  uint8_t state_ = 0;
  uint16_t index_ = 0;
  uint16_t expected_ = 0;
  uint8_t body_[12U + OTA_MAX_PAYLOAD + 4U];

  void reset() { state_ = 0; index_ = 0; expected_ = 0; }
  static uint16_t readLe16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
  static uint32_t readLe32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }
  static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    while (len--) {
      crc ^= *data++;
      for (uint8_t bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc ^ 0xFFFFFFFFUL;
  }
  void onFrame();
};

static FrameParser parser;
static ReceivedFrame response;
static Manifest cachedManifest;
static uint32_t txSequence = 0;
static uint32_t currentStm32Version = 0;
static uint32_t nextCheckAt = 0;
static uint32_t nextPrepareAt = 0;
static uint8_t retryStep = 0;
static bool appOnline = false;
static bool bootloaderOnline = false;
static bool installAccepted = false;
static bool bootloaderUpdateRequested = false;
static bool manualCheck = false;

static void writeLe16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void writeLe32(uint8_t *p, uint32_t v) {
  p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static uint32_t crcUpdate(uint32_t crc, uint8_t byte) {
  crc ^= byte;
  for (uint8_t bit = 0; bit < 8; ++bit)
    crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
  return crc;
}

static void sendFrame(uint8_t type, uint32_t offset, const uint8_t *payload, uint16_t len) {
  uint8_t header[12];
  uint32_t crc = 0xFFFFFFFFUL;
  header[0] = OTA_PROTOCOL_VERSION; header[1] = type;
  writeLe32(header + 2, txSequence++); writeLe32(header + 6, offset); writeLe16(header + 10, len);
  Serial.write(OTA_SOF0); Serial.write(OTA_SOF1);
  for (uint8_t b : header) { crc = crcUpdate(crc, b); Serial.write(b); }
  for (uint16_t i = 0; i < len; ++i) { crc = crcUpdate(crc, payload[i]); Serial.write(payload[i]); }
  crc ^= 0xFFFFFFFFUL;
  uint8_t crcBytes[4]; writeLe32(crcBytes, crc); Serial.write(crcBytes, sizeof(crcBytes));
  Serial.flush();
}

void FrameParser::onFrame() {
  const uint8_t type = body_[1];
  const uint32_t sequence = readLe32(body_ + 2);
  const uint32_t offset = readLe32(body_ + 6);
  const uint16_t length = readLe16(body_ + 10);
  const uint8_t *payload = body_ + 12;

  if (type == MSG_APP_BOOT_OK && length == 4) {
    currentStm32Version = readLe32(payload); appOnline = true; bootloaderOnline = false;
    installAccepted = false;
    bootloaderUpdateRequested = false;
    OTA_DEBUG_PRINTF("[UART] RX APP_BOOT_OK version=%lu\r\n",
                     static_cast<unsigned long>(currentStm32Version));
    if (cachedManifest.valid && currentStm32Version >= cachedManifest.versionCode) {
      LittleFS.remove(OTA_FIRMWARE_FILE); LittleFS.remove(OTA_MANIFEST_FILE);
      cachedManifest = Manifest();
      OTA_DEBUG_PRINTLN("[CACHE] installed version confirmed; cache removed");
    }
    return;
  }
  if (type == MSG_BL_HELLO && length >= 8) {
    uint32_t bootloaderVersion = readLe32(payload);
    currentStm32Version = readLe32(payload + 4); bootloaderOnline = true; appOnline = false;
    if (length >= 20) {
      bootloaderUpdateRequested = payload[17] != 0U;
      OTA_DEBUG_PRINTF("[UART] RX BL_HELLO bootloader=%lu current_version=%lu app_valid=%u update_request=%u recovery=%u metadata=%u",
                       static_cast<unsigned long>(bootloaderVersion),
                       static_cast<unsigned long>(currentStm32Version),
                       static_cast<unsigned int>(payload[16]),
                       static_cast<unsigned int>(payload[17]),
                       static_cast<unsigned int>(payload[18]),
                       static_cast<unsigned int>(payload[19]));
      if (length >= 24) {
        OTA_DEBUG_PRINTF(" rx_bytes=%lu", static_cast<unsigned long>(readLe32(payload + 20)));
      }
      if (length >= 28) {
        OTA_DEBUG_PRINTF(" rx_dropped=%lu", static_cast<unsigned long>(readLe32(payload + 24)));
      }
      OTA_DEBUG_PRINTLN("");
    } else {
      bootloaderUpdateRequested = false;
      OTA_DEBUG_PRINTF("[UART] RX BL_HELLO bootloader=%lu current_version=%lu\r\n",
                       static_cast<unsigned long>(bootloaderVersion),
                       static_cast<unsigned long>(currentStm32Version));
    }
  } else if (type == MSG_BL_TRACE && length >= 5) {
    OTA_DEBUG_PRINTF("[UART] RX BL_TRACE stage=0x%02X value=%lu offset=%lu\r\n",
                     static_cast<unsigned int>(payload[0]),
                     static_cast<unsigned long>(readLe32(payload + 1)),
                     static_cast<unsigned long>(offset));
  } else if (type == MSG_OTA_ACCEPT) {
    installAccepted = true;
    OTA_DEBUG_PRINTLN("[UART] RX OTA_ACCEPT");
  } else if (type == MSG_OTA_CHECK) {
    manualCheck = true;
    OTA_DEBUG_PRINTLN("[UART] RX OTA_CHECK");
  }

  if (type == MSG_ACK || type == MSG_NACK || type == MSG_RESULT || type == MSG_BL_HELLO) {
    response.type = type; response.sequence = sequence; response.offset = offset;
    response.length = (length < sizeof(response.payload)) ? length : sizeof(response.payload);
    memcpy(response.payload, payload, response.length); response.ready = true;
  }
}

static void pollSerial() {
  while (Serial.available() > 0) parser.feed(static_cast<uint8_t>(Serial.read()));
}

static bool waitResponse(uint8_t wantedType, uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    pollSerial();
    if (response.ready) {
      if (response.type == wantedType) { response.ready = false; return true; }
      if (response.type == MSG_NACK || response.type == MSG_RESULT) return true;
      response.ready = false;
    }
    delay(1); yield();
  }
  return false;
}

static int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static bool decodeBase64(const String &input, uint8_t *output, size_t expected) {
  uint32_t accumulator = 0; uint8_t bits = 0; size_t written = 0;
  for (size_t i = 0; i < input.length(); ++i) {
    if (input[i] == '=') break;
    int value = base64Value(input[i]); if (value < 0) continue;
    accumulator = (accumulator << 6) | uint32_t(value); bits += 6;
    if (bits >= 8) { bits -= 8; if (written >= expected) return false; output[written++] = accumulator >> bits; }
  }
  return written == expected;
}

static bool decodeHex32(const String &hex, uint8_t out[32]) {
  if (hex.length() != 64) return false;
  for (size_t i = 0; i < 32; ++i) {
    int hi = isDigit(hex[i * 2]) ? hex[i * 2] - '0' : tolower(hex[i * 2]) - 'a' + 10;
    int lo = isDigit(hex[i * 2 + 1]) ? hex[i * 2 + 1] - '0' : tolower(hex[i * 2 + 1]) - 'a' + 10;
    if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return false;
    out[i] = uint8_t((hi << 4) | lo);
  }
  return true;
}

static bool parseManifest(const String &json, Manifest &manifest) {
  DynamicJsonDocument document(1536);
  DeserializationError jsonError = deserializeJson(document, json);
  if (jsonError) {
    OTA_DEBUG_PRINTF("[MANIFEST] JSON error: %s\r\n", jsonError.c_str());
    return false;
  }
  if (document["schema"].as<int>() != 1 ||
      String(document["target"].as<const char *>()) != OTA_TARGET) {
    OTA_DEBUG_PRINTLN("[MANIFEST] schema or target mismatch");
    return false;
  }
  manifest.versionCode = document["version_code"].as<uint32_t>();
  manifest.size = document["size"].as<uint32_t>();
  manifest.version = String(document["version"].as<const char *>());
  manifest.sha256 = String(document["sha256"].as<const char *>());
  manifest.url = String(document["url"].as<const char *>());
  String signature = String(document["signature"].as<const char *>());
  manifest.valid = manifest.versionCode != 0 && manifest.size != 0 && manifest.size <= OTA_APP_MAX_SIZE &&
                   manifest.url.startsWith("https://") && decodeHex32(manifest.sha256, manifest.hash) &&
                   decodeBase64(signature, manifest.signature, sizeof(manifest.signature));
  if (!manifest.valid) {
    OTA_DEBUG_PRINTLN("[MANIFEST] invalid fields/hash/signature encoding");
  }
  return manifest.valid;
}

static bool loadCachedManifest() {
  if (!LittleFS.exists(OTA_MANIFEST_FILE) || !LittleFS.exists(OTA_FIRMWARE_FILE)) return false;
  File file = LittleFS.open(OTA_MANIFEST_FILE, "r"); if (!file) return false;
  String json = file.readString(); file.close();
  if (!parseManifest(json, cachedManifest)) return false;
  File firmware = LittleFS.open(OTA_FIRMWARE_FILE, "r");
  bool sizeOk = firmware && firmware.size() == cachedManifest.size; firmware.close();
  if (!sizeOk) { cachedManifest = Manifest(); return false; }
  return true;
}

static bool ensureNetworkAndTime() {
  if (WiFi.status() != WL_CONNECTED) {
    OTA_DEBUG_PRINTF("[WIFI] connecting SSID=%s\r\n", OTA_WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) { delay(250); yield(); }
  }
  if (WiFi.status() != WL_CONNECTED) {
    OTA_DEBUG_PRINTF("[WIFI] connect failed status=%d\r\n",
                     static_cast<int>(WiFi.status()));
    return false;
  }
  OTA_DEBUG_PRINTF("[WIFI] connected IP=%s RSSI=%d dBm\r\n",
                   WiFi.localIP().toString().c_str(),
                   WiFi.RSSI());

  OTA_DEBUG_PRINTLN("[TIME] synchronizing NTP");
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  uint32_t start = millis();
  while (time(nullptr) < 1700000000L && millis() - start < 15000UL) { delay(200); yield(); }
  if (time(nullptr) < 1700000000L) {
    OTA_DEBUG_PRINTLN("[TIME] NTP timeout");
    return false;
  }
  OTA_DEBUG_PRINTLN("[TIME] NTP synchronized");
  return true;
}

static bool httpsGetText(const String &url, String &body) {
  OTA_DEBUG_PRINTF("[HTTP] GET manifest %s\r\n", url.c_str());
  body = "";
  for (uint8_t attempt = 1U; attempt <= OTA_HTTP_MAX_ATTEMPTS; ++attempt) {
    BearSSL::X509List trustAnchor(OTA_ROOT_CA);
    BearSSL::WiFiClientSecure client;
    client.setTrustAnchors(&trustAnchor);
    /*
     * GitHub Pages does not reliably negotiate TLS maximum-fragment-length.
     * Use a full TLS-record receive buffer for the manifest as well as for
     * firmware, otherwise a small 512-byte buffer can fail during connect.
     */
    client.setBufferSizes(OTA_TLS_DOWNLOAD_RX_SIZE,
                          OTA_TLS_DOWNLOAD_TX_SIZE);
    HTTPClient http;
    http.setTimeout(15000);
    http.setReuse(false);
    OTA_DEBUG_PRINTF("[HTTP] manifest attempt=%u heap=%lu max_block=%lu\r\n",
                     static_cast<unsigned int>(attempt),
                     static_cast<unsigned long>(ESP.getFreeHeap()),
                     static_cast<unsigned long>(ESP.getMaxFreeBlockSize()));
    if (!http.begin(client, url)) {
      OTA_DEBUG_PRINTLN("[HTTP] begin failed");
    } else {
      yield();
      ESP.wdtFeed();
      int status = http.GET();
      if (status == HTTP_CODE_OK) {
        body = http.getString();
        OTA_DEBUG_PRINTF("[HTTP] manifest OK bytes=%u\r\n",
                         static_cast<unsigned int>(body.length()));
        http.end();
        return true;
      }

      char tlsError[160] = {0};
      int tlsCode = client.getLastSSLError(tlsError, sizeof(tlsError));
      String httpError = HTTPClient::errorToString(status);
      OTA_DEBUG_PRINTF("[HTTP] manifest failed status=%d error=%s TLS=%d %s\r\n",
                       status,
                       httpError.c_str(),
                       tlsCode,
                       tlsError);
      http.end();
      client.stop();
    }
    if (attempt < OTA_HTTP_MAX_ATTEMPTS) {
      delay(500U * attempt);
      yield();
    }
  }
  return false;
}

static bool downloadFirmware(const Manifest &manifest, const String &manifestJson) {
  OTA_DEBUG_PRINTF("[DOWNLOAD] start version=%lu size=%lu\r\n",
                   static_cast<unsigned long>(manifest.versionCode),
                   static_cast<unsigned long>(manifest.size));
  BearSSL::X509List trustAnchor(OTA_ROOT_CA);
  BearSSL::WiFiClientSecure client;
  client.setTrustAnchors(&trustAnchor);
  /*
   * Do not reduce the receive buffer to 512 bytes for firmware downloads.
   * GitHub Pages can send the binary body in a full-size TLS record. A 512
   * byte buffer relies on MFLN negotiation, which the server may not accept;
   * the headers then succeed but BearSSL disconnects before body byte zero.
   */
  client.setBufferSizes(OTA_TLS_DOWNLOAD_RX_SIZE,
                        OTA_TLS_DOWNLOAD_TX_SIZE);
  HTTPClient http; http.setTimeout(15000);
  if (!http.begin(client, manifest.url)) {
    OTA_DEBUG_PRINTLN("[DOWNLOAD] HTTP begin failed");
    return false;
  }
  int status = http.GET();
  if (status != HTTP_CODE_OK || http.getSize() != static_cast<int>(manifest.size)) {
    OTA_DEBUG_PRINTF("[DOWNLOAD] HTTP/size failed status=%d got=%d expected=%lu\r\n",
                     status,
                     http.getSize(),
                     static_cast<unsigned long>(manifest.size));
    http.end();
    return false;
  }

  LittleFS.remove(OTA_TEMP_FILE);
  File output = LittleFS.open(OTA_TEMP_FILE, "w");
  if (!output) {
    OTA_DEBUG_PRINTLN("[DOWNLOAD] cannot open temporary file");
    http.end();
    return false;
  }
  br_sha256_context hashContext; br_sha256_init(&hashContext);
  WiFiClient *stream = http.getStreamPtr();
  if (stream == nullptr) {
    char tlsError[160] = {0};
    int tlsCode = client.getLastSSLError(tlsError, sizeof(tlsError));
    OTA_DEBUG_PRINTF("[DOWNLOAD] no response stream TLS=%d %s\r\n",
                     tlsCode,
                     tlsError);
    output.close();
    http.end();
    LittleFS.remove(OTA_TEMP_FILE);
    return false;
  }
  uint8_t buffer[1024];
  uint32_t total = 0;
  uint32_t lastDataAt = millis();
  uint32_t nextProgress = 8192U;

  OTA_DEBUG_PRINTF("[DOWNLOAD] HTTP OK content_length=%d\r\n",
                   http.getSize());

  /*
   * Do not use http.connected() as the loop condition. GitHub may close the
   * HTTP connection after sending the response while BearSSL still has body
   * bytes buffered. In that case connected() is false but available() can
   * become non-zero after the TLS layer is serviced.
   */
  while (total < manifest.size) {
    size_t available = stream->available();
    if (!available) {
      if (millis() - lastDataAt >= OTA_DOWNLOAD_IDLE_TIMEOUT_MS) {
        char tlsError[160] = {0};
        int tlsCode = client.getLastSSLError(tlsError, sizeof(tlsError));
        OTA_DEBUG_PRINTF("[DOWNLOAD] stream timeout connected=%d received=%lu\r\n",
                         http.connected() ? 1 : 0,
                         static_cast<unsigned long>(total));
        OTA_DEBUG_PRINTF("[DOWNLOAD] TLS=%d %s\r\n",
                         tlsCode,
                         tlsError);
        break;
      }
      delay(1);
      yield();
      continue;
    }
    size_t remaining = manifest.size - total;
    size_t wanted = available < remaining ? available : remaining;
    if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
    size_t received = stream->readBytes(buffer, wanted);
    if (!received || output.write(buffer, received) != received) {
      OTA_DEBUG_PRINTLN("[DOWNLOAD] stream or flash write failed");
      output.close(); http.end(); LittleFS.remove(OTA_TEMP_FILE); return false;
    }
    br_sha256_update(&hashContext, buffer, received);
    total += received;
    lastDataAt = millis();
    if (total >= nextProgress || total == manifest.size) {
      OTA_DEBUG_PRINTF("[DOWNLOAD] progress=%lu/%lu\r\n",
                       static_cast<unsigned long>(total),
                       static_cast<unsigned long>(manifest.size));
      nextProgress += 8192U;
    }
    yield();
  }
  output.close(); http.end();
  uint8_t digest[32]; br_sha256_out(&hashContext, digest);
  if (total != manifest.size || memcmp(digest, manifest.hash, 32) != 0) {
    OTA_DEBUG_PRINTF("[DOWNLOAD] hash/length failed received=%lu\r\n",
                     static_cast<unsigned long>(total));
    LittleFS.remove(OTA_TEMP_FILE);
    return false;
  }

  LittleFS.remove(OTA_FIRMWARE_FILE);
  if (!LittleFS.rename(OTA_TEMP_FILE, OTA_FIRMWARE_FILE)) {
    OTA_DEBUG_PRINTLN("[DOWNLOAD] cache rename failed");
    return false;
  }
  File manifestFile = LittleFS.open(OTA_MANIFEST_FILE, "w");
  if (!manifestFile || manifestFile.print(manifestJson) != manifestJson.length()) {
    OTA_DEBUG_PRINTLN("[DOWNLOAD] manifest cache write failed");
    manifestFile.close();
    return false;
  }
  manifestFile.close();
  OTA_DEBUG_PRINTLN("[DOWNLOAD] firmware cached and verified");
  return true;
}

static bool checkForUpdate() {
  OTA_DEBUG_PRINTF("[OTA] check started; STM32 version=%lu\r\n",
                   static_cast<unsigned long>(currentStm32Version));
  if (!ensureNetworkAndTime()) {
    OTA_DEBUG_PRINTLN("[OTA] check failed: network/time");
    return false;
  }
  String json;
  if (!httpsGetText(OTA_MANIFEST_URL, json)) {
    OTA_DEBUG_PRINTLN("[OTA] check failed: manifest HTTP");
    return false;
  }
  Manifest candidate;
  if (!parseManifest(json, candidate)) {
    OTA_DEBUG_PRINTLN("[OTA] check failed: invalid manifest");
    return false;
  }
  OTA_DEBUG_PRINTF("[MANIFEST] version=%lu size=%lu current=%lu\r\n",
                   static_cast<unsigned long>(candidate.versionCode),
                   static_cast<unsigned long>(candidate.size),
                   static_cast<unsigned long>(currentStm32Version));
  if (candidate.versionCode <= currentStm32Version) {
    OTA_DEBUG_PRINTLN("[OTA] no newer firmware");
    return true;
  }
  if (cachedManifest.valid &&
      candidate.versionCode == cachedManifest.versionCode &&
      LittleFS.exists(OTA_FIRMWARE_FILE)) {
    OTA_DEBUG_PRINTLN("[OTA] newer firmware already cached");
    return true;
  }
  if (!downloadFirmware(candidate, json)) {
    OTA_DEBUG_PRINTLN("[OTA] check failed: firmware download");
    return false;
  }
  cachedManifest = candidate;
  OTA_DEBUG_PRINTLN("[OTA] update ready");
  return true;
}

static const char *messageTypeName(uint8_t type) {
  switch (type) {
    case MSG_BEGIN: return "BEGIN";
    case MSG_DATA: return "DATA";
    case MSG_END: return "END";
    case MSG_ACK: return "ACK";
    case MSG_NACK: return "NACK";
    case MSG_RESULT: return "RESULT";
    case MSG_BL_HELLO: return "BL_HELLO";
    case MSG_BL_TRACE: return "BL_TRACE";
    default: return "UNKNOWN";
  }
}

static const char *resultCodeName(uint8_t code) {
  switch (code) {
    case 0: return "OK";
    case 1: return "BAD_FRAME";
    case 2: return "BAD_STATE";
    case 3: return "BAD_TARGET";
    case 4: return "BAD_VERSION";
    case 5: return "BAD_SIZE";
    case 6: return "BAD_OFFSET";
    case 7: return "FLASH_ERROR";
    case 8: return "HASH_ERROR";
    case 9: return "SIGNATURE_ERROR";
    case 10: return "TIMEOUT";
    default: return "UNKNOWN";
  }
}

static bool sendWithAck(uint8_t type, uint32_t offset, const uint8_t *data, uint16_t length,
                        uint32_t timeoutMs = OTA_ACK_TIMEOUT_MS) {
  const uint32_t expectedAckOffset =
      (type == MSG_DATA) ? offset + static_cast<uint32_t>(length) : offset;
  for (uint8_t attempt = 0; attempt < OTA_MAX_RETRIES; ++attempt) {
    response.ready = false;
    OTA_DEBUG_PRINTF("[TRANSFER] TX %s offset=%lu len=%u attempt=%u\r\n",
                     messageTypeName(type),
                     static_cast<unsigned long>(offset),
                     static_cast<unsigned int>(length),
                     static_cast<unsigned int>(attempt + 1U));
    sendFrame(type, offset, data, length);

    if (!waitResponse(MSG_ACK, timeoutMs)) {
      OTA_DEBUG_PRINTF("[TRANSFER] %s ACK timeout\r\n",
                       messageTypeName(type));
      continue;
    }

    if (response.length >= 1U) {
      OTA_DEBUG_PRINTF("[TRANSFER] RX %s offset=%lu result=%u(%s)\r\n",
                       messageTypeName(response.type),
                       static_cast<unsigned long>(response.offset),
                       static_cast<unsigned int>(response.payload[0]),
                       resultCodeName(response.payload[0]));
    } else {
      OTA_DEBUG_PRINTF("[TRANSFER] RX %s offset=%lu empty payload\r\n",
                       messageTypeName(response.type),
                       static_cast<unsigned long>(response.offset));
    }

    if (response.type == MSG_ACK &&
        response.length >= 1U &&
        response.payload[0] == 0U) {
      if (response.offset != expectedAckOffset) {
        OTA_DEBUG_PRINTF("[TRANSFER] ignore stale ACK expected_offset=%lu\r\n",
                         static_cast<unsigned long>(expectedAckOffset));
        continue;
      }
      return true;
    }
  }
  return false;
}

static bool transferFirmware() {
  static uint32_t transferAttempt = 0;
  uint8_t begin[104];
  uint32_t nextProgress = 8192U;

  transferAttempt++;
  OTA_DEBUG_PRINTF("[TRANSFER] start attempt=%lu version=%lu size=%lu\r\n",
                   static_cast<unsigned long>(transferAttempt),
                   static_cast<unsigned long>(cachedManifest.versionCode),
                   static_cast<unsigned long>(cachedManifest.size));
  writeLe32(begin, cachedManifest.versionCode); writeLe32(begin + 4, cachedManifest.size);
  memcpy(begin + 8, cachedManifest.hash, 32); memcpy(begin + 40, cachedManifest.signature, 64);
  /* BEGIN includes P-256 verification and application erase on STM32. */
  if (!sendWithAck(MSG_BEGIN, 0, begin, sizeof(begin), 120000UL)) {
    OTA_DEBUG_PRINTLN("[TRANSFER] BEGIN failed");
    return false;
  }
  OTA_DEBUG_PRINTLN("[TRANSFER] BEGIN accepted");

  File firmware = LittleFS.open(OTA_FIRMWARE_FILE, "r");
  if (!firmware) {
    OTA_DEBUG_PRINTLN("[TRANSFER] cached firmware open failed");
    return false;
  }
  uint8_t data[OTA_DATA_CHUNK_SIZE]; uint32_t offset = 0;
  while (offset < cachedManifest.size) {
    uint32_t remaining = cachedManifest.size - offset;
    uint16_t chunk = static_cast<uint16_t>(remaining > sizeof(data) ? sizeof(data) : remaining);
    if (firmware.read(data, chunk) != chunk) {
      OTA_DEBUG_PRINTF("[TRANSFER] cache read failed offset=%lu\r\n",
                       static_cast<unsigned long>(offset));
      firmware.close();
      return false;
    }
    if (!sendWithAck(MSG_DATA, offset, data, chunk)) {
      OTA_DEBUG_PRINTF("[TRANSFER] DATA failed offset=%lu\r\n",
                       static_cast<unsigned long>(offset));
      firmware.close();
      return false;
    }
    offset += chunk; yield();
    if (offset >= nextProgress || offset == cachedManifest.size) {
      OTA_DEBUG_PRINTF("[TRANSFER] progress=%lu/%lu\r\n",
                       static_cast<unsigned long>(offset),
                       static_cast<unsigned long>(cachedManifest.size));
      nextProgress += 8192U;
    }
  }
  firmware.close();
  response.ready = false;
  OTA_DEBUG_PRINTF("[TRANSFER] TX END offset=%lu\r\n",
                   static_cast<unsigned long>(offset));
  sendFrame(MSG_END, offset, nullptr, 0);
  if (!waitResponse(MSG_RESULT, 10000UL)) {
    OTA_DEBUG_PRINTLN("[TRANSFER] RESULT timeout");
    return false;
  }
  if (response.length >= 1U) {
    OTA_DEBUG_PRINTF("[TRANSFER] RX %s offset=%lu result=%u(%s)\r\n",
                     messageTypeName(response.type),
                     static_cast<unsigned long>(response.offset),
                     static_cast<unsigned int>(response.payload[0]),
                     resultCodeName(response.payload[0]));
  }
  if (response.type != MSG_RESULT ||
      response.length < 1U ||
      response.payload[0] != 0U) {
    OTA_DEBUG_PRINTLN("[TRANSFER] END verification failed");
    return false;
  }
  bootloaderOnline = false;
  installAccepted = false;
  bootloaderUpdateRequested = false;
  OTA_DEBUG_PRINTLN("[TRANSFER] complete; waiting for APP_BOOT_OK");
  return true;
}

static void scheduleAfterCheck(bool success) {
  static const uint32_t retryDelay[] = {60000UL, 300000UL, 900000UL, 1800000UL};
  if (success) { retryStep = 0; nextCheckAt = millis() + OTA_CHECK_PERIOD_MS; }
  else { uint8_t index = retryStep < 3 ? retryStep : 3; nextCheckAt = millis() + retryDelay[index]; if (retryStep < 3) ++retryStep; }
}

void setup() {
  Serial1.begin(OTA_DEBUG_BAUD);
  delay(10);
  OTA_DEBUG_PRINTLN("");
  OTA_DEBUG_PRINTLN("[BOOT] ESP8266 OTA gateway started");
  OTA_DEBUG_PRINTF("[BOOT] build=%s data_chunk=%u update_request_resume=on\r\n",
                   OTA_GATEWAY_BUILD_ID,
                   static_cast<unsigned int>(OTA_DATA_CHUNK_SIZE));
  OTA_DEBUG_PRINTF("[BOOT] debug GPIO2 baud=%u\r\n",
                   static_cast<unsigned int>(OTA_DEBUG_BAUD));
  OTA_DEBUG_PRINTF("[BOOT] configuration=%s\r\n",
                   OTA_CONFIGURATION_READY ? "ready" : "missing");

  Serial.begin(115200);
  Serial.setTimeout(2000);
  bool fsReady = LittleFS.begin();
  bool cacheReady = fsReady && loadCachedManifest();
  OTA_DEBUG_PRINTF("[FS] LittleFS=%s cache=%s\r\n",
                   fsReady ? "ready" : "failed",
                   cacheReady ? "ready" : "empty");
  nextCheckAt = 1; nextPrepareAt = 1;
}

void loop() {
  pollSerial();
  if (!OTA_CONFIGURATION_READY) { delay(1000); return; }

  if (bootloaderOnline && cachedManifest.valid &&
      cachedManifest.versionCode > currentStm32Version &&
      (installAccepted || bootloaderUpdateRequested || currentStm32Version == 0U)) {
    transferFirmware(); delay(250); return;
  }

  const uint32_t now = millis();
  if (manualCheck || static_cast<int32_t>(now - nextCheckAt) >= 0) {
    manualCheck = false; scheduleAfterCheck(checkForUpdate());
  }

  if (cachedManifest.valid && appOnline && cachedManifest.versionCode > currentStm32Version &&
      static_cast<int32_t>(now - nextPrepareAt) >= 0) {
    uint8_t prepare[8]; writeLe32(prepare, cachedManifest.versionCode); writeLe32(prepare + 4, cachedManifest.size);
    OTA_DEBUG_PRINTF("[UART] TX OTA_PREPARE version=%lu size=%lu\r\n",
                     static_cast<unsigned long>(cachedManifest.versionCode),
                     static_cast<unsigned long>(cachedManifest.size));
    sendFrame(MSG_OTA_PREPARE, 0, prepare, sizeof(prepare));
    nextPrepareAt = now + OTA_PREPARE_PERIOD_MS;
  }
  delay(2); yield();
}

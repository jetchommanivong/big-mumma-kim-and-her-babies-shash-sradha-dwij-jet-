// ESP32-CAM: takes a photo when ITS gun scores a hit ("HIT1" or "HIT2" over ESP-NOW)
// and saves it to the microSD card as /photo_1.jpg, /photo_2.jpg, ...
//
// Board: Tools > Board > "AI Thinker ESP32-CAM"
// Upload through the ESP32-CAM-MB base (hold IO0 if it gets stuck on "Connecting...")
// Serial Monitor: 115200, then press RST on the camera
//
// No WiFi network or router needed. The target ESP32 broadcasts "HIT1"/"HIT2"
// and each camera only reacts to its own gun number.

#include "esp_camera.h"
#include "img_converters.h"
#include "esp_log.h"
#include <WiFi.h>
#include <esp_now.h>
#include "FS.h"
#include "SD_MMC.h"
#include <Preferences.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ---- AI Thinker ESP32-CAM camera pins ----
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// >>> SET THIS TO MATCH THE GUN THIS CAMERA IS MOUNTED ON <<<
const int GUN_ID = 2;

// Manual exposure = faster shutter, less blur on moving players.
// If photos are too dark raise EXPOSURE (max ~1200) or GAIN (max 30). Set false for auto.
const bool MANUAL_EXPOSURE = true;
const int EXPOSURE = 150;
const int GAIN = 10;

#define FLASH_LED  4    // big white LED on the front (kept off)
#define RED_LED   33    // small red LED on the back (LOW = on)

// Photo size for raw-mode cameras (GC2145 type).
// If you still see "FB-SIZE" or get no photos, change to FRAMESIZE_QVGA (320x240).
// If it works well, try FRAMESIZE_SVGA (800x600) for sharper photos.
#define RAW_FRAME_SIZE FRAMESIZE_VGA   // 640x480

// FAST_CAPTURE = the camera records non-stop into 2 buffers, so on a hit the newest
// frame is already waiting (photo is from ~0.1s after the hit instead of ~1s).
// If you start getting black or broken photos again, set this to false.
const bool FAST_CAPTURE = true;

// Extra wait after a hit before taking the photo, e.g. 300 to catch the reaction. 0 = instant.
const unsigned long PHOTO_DELAY_MS = 0;

// Status blinks on the big white LED, so you can tell what's happening without Serial Monitor
const bool DEBUG_BLINKS = true;   // set false for the real game

void blinkFlash(int times) {
  if (!DEBUG_BLINKS) return;
  for (int i = 0; i < times; i++) {
    digitalWrite(4, HIGH);
    delay(60);
    digitalWrite(4, LOW);
    delay(300);
  }
}

esp_err_t cameraError = ESP_OK;
volatile bool photoRequested = false;
volatile unsigned long hitAt = 0;
bool sdReady = false;
Preferences prefs;

// Called by ESP-NOW when a message arrives. Keep it short: just set a flag.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  // Only react to hits made by this camera's gun
  if (len >= 4 && memcmp(data, "HIT", 3) == 0 && data[3] == (uint8_t)('0' + GUN_ID)) {
    if (!photoRequested) hitAt = millis();
    photoRequested = true;
  }
}

// Some newer ESP32-CAM boards ship with a camera chip (e.g. GC2145) that can't make
// JPEGs itself (that's error 0x106). Then we capture raw pixels and convert to JPEG ourselves.
bool needsJpegConversion = false;

esp_err_t initCamera(pixformat_t format) {
  camera_config_t config = {};   // start with every setting zeroed, then fill in
  config.sccb_i2c_port = 0;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = format;
  config.frame_size   = FRAMESIZE_SVGA;     // 800x600; try FRAMESIZE_UXGA for 1600x1200
  config.jpeg_quality = 12;                 // lower number = better quality, bigger file
  config.grab_mode    = CAMERA_GRAB_LATEST; // always use the newest frame, not an old one

  if (psramFound()) {
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.frame_size = FRAMESIZE_VGA;
  }

  // Raw mode (GC2145-type chips): slower clock, smaller frames, one buffer.
  // Too fast a clock gives broken frames ("FB-SIZE" messages) and black photos.
  if (format != PIXFORMAT_JPEG) {
    config.xclk_freq_hz = 10000000;
    config.frame_size   = RAW_FRAME_SIZE;
    if (FAST_CAPTURE && psramFound()) {
      config.fb_count    = 2;                    // keep recording in the background
      config.fb_location = CAMERA_FB_IN_PSRAM;
      config.grab_mode   = CAMERA_GRAB_LATEST;   // hand over the newest frame straight away
    } else {
      config.fb_count    = 1;
      config.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
    }
  }

  return esp_camera_init(&config);
}

// Grab a few frames so the camera's auto-exposure can adjust (first frames are often black)
void warmUpCamera() {
  for (int i = 0; i < 15; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(50);
  }
}

// Get a frame, skipping broken ones (wrong size = incomplete frame)
camera_fb_t *getGoodFrame() {
  for (int attempt = 0; attempt < 6; attempt++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) continue;
    if (!needsJpegConversion) return fb;                  // JPEG camera: sizes vary, accept it
    if (fb->len == (size_t)fb->width * fb->height * 2) {  // RGB565 = 2 bytes per pixel
      return fb;
    }
    esp_camera_fb_return(fb);                              // incomplete frame, try again
  }
  return NULL;
}

void setFastShutter() {
  if (!MANUAL_EXPOSURE) return;
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;
  if (s->set_exposure_ctrl) s->set_exposure_ctrl(s, 0);   // auto exposure off
  if (s->set_aec_value)     s->set_aec_value(s, EXPOSURE);
  if (s->set_gain_ctrl)     s->set_gain_ctrl(s, 0);       // auto gain off
  if (s->set_agc_gain)      s->set_agc_gain(s, GAIN);
}

bool startCamera() {
  esp_err_t err = initCamera(PIXFORMAT_JPEG);

  if (err == ESP_ERR_NOT_SUPPORTED) {
    Serial.println("This camera chip can't make JPEGs itself, switching to raw + convert");
    esp_camera_deinit();
    delay(100);
    err = initCamera(PIXFORMAT_RGB565);
    needsJpegConversion = (err == ESP_OK);
  }

  if (err != ESP_OK) {
    Serial.printf("Camera init failed, error 0x%x, PSRAM found: %s\n", err, psramFound() ? "yes" : "no");
    cameraError = err;
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) Serial.printf("Camera chip ID: 0x%x\n", s->id.PID);
  return true;
}

void takeAndSavePhoto() {
  camera_fb_t *fb;
  if (!FAST_CAPTURE) {
    // Slow mode: the single buffer holds an old frame, throw it away first
    fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }

  fb = getGoodFrame();
  if (fb) Serial.printf("(captured %lu ms after the hit) ", millis() - hitAt);
  if (!fb) {
    Serial.println("Capture failed (only broken frames)");
    return;
  }

  digitalWrite(RED_LED, LOW);   // red LED on while saving

  // Get JPEG bytes: straight from the camera, or converted from raw pixels
  uint8_t *jpgBuf = fb->buf;
  size_t jpgLen = fb->len;
  bool converted = false;
  String ext = ".jpg";
  if (needsJpegConversion) {
    unsigned long t0 = millis();
    if (frame2jpg(fb, 80, &jpgBuf, &jpgLen)) {
      Serial.printf("(converted to JPEG in %lu ms) ", millis() - t0);
    } else if (frame2bmp(fb, &jpgBuf, &jpgLen)) {
      // JPEG conversion failed: save as BMP instead (no compression, so much less work)
      ext = ".bmp";
      Serial.print("(JPEG failed, saved as BMP) ");
    } else {
      Serial.println("Image conversion failed");
      esp_camera_fb_return(fb);
      digitalWrite(RED_LED, HIGH);
      return;
    }
    converted = true;
    esp_camera_fb_return(fb);   // raw frame no longer needed once converted
    fb = NULL;
  }

  if (sdReady) {
    int n = prefs.getInt("count", 0) + 1;
    prefs.putInt("count", n);
    String path = "/photo_" + String(n) + ext;

    File file = SD_MMC.open(path, FILE_WRITE);
    if (file) {
      file.write(jpgBuf, jpgLen);
      file.close();
      Serial.printf("Saved %s (%u bytes)\n", path.c_str(), jpgLen);
    } else {
      Serial.println("Couldn't open file on SD card");
    }
  } else {
    Serial.printf("Photo taken (%u bytes) but no SD card to save it to\n", jpgLen);
  }

  if (converted) free(jpgBuf);
  if (fb) esp_camera_fb_return(fb);   // JPEG straight from camera: return frame after saving
  digitalWrite(RED_LED, HIGH);  // red LED off
  blinkFlash(1);                // quick flash after each photo (taken after, so it isn't in the shot)
}

void setup() {
  // ESP32-CAMs often reset themselves in a loop on USB power ("brownout").
  // This turns that safety reset off so the board can start up.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESP32-CAM hit photographer ===");

  pinMode(RED_LED, OUTPUT);
  digitalWrite(RED_LED, HIGH);

  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW);

  if (!startCamera()) {
    Serial.println("Camera failed to start. Check the ribbon cable is clicked in, then press RST.");
    // Camera problem. Which one, by blink count (repeating):
    //   3 = camera not detected (ribbon / camera module)
    //   5 = not enough memory (PSRAM off: wrong board selected in Tools)
    //   6 = some other camera error
    int code = 6;
    if (cameraError == ESP_ERR_NOT_FOUND || cameraError == 0x20001) code = 3;
    else if (cameraError == ESP_ERR_NO_MEM || !psramFound()) code = 5;
    while (true) {
      Serial.printf("Camera init failed, error 0x%x\n", cameraError);   // repeats so you can catch it anytime
      blinkFlash(code);
      delay(2000);
    }
  }
  Serial.println("Camera OK, warming up...");
  esp_log_level_set("cam_hal", ESP_LOG_NONE);   // hide the "FB-SIZE" spam
  warmUpCamera();
  setFastShutter();

  // 1-bit SD mode so the big white flash LED doesn't fire on every save
  sdReady = SD_MMC.begin("/sdcard", true);
  Serial.println(sdReady ? "SD card OK" : "No SD card found (photos won't be saved)");
  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW);

  prefs.begin("camera", false);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed to start");
    while (true) { blinkFlash(4); delay(2000); }   // 4 blinks, repeating = wireless problem
  }
  esp_now_register_recv_cb(onReceive);

  delay(500);
  blinkFlash(sdReady ? 1 : 2);   // 1 blink = all good, 2 blinks = SD card problem
  Serial.printf("Waiting for hits from gun %d...\n", GUN_ID);
}

void loop() {
  if (photoRequested) {
    photoRequested = false;
    Serial.print("HIT received -> ");
    if (PHOTO_DELAY_MS > 0) delay(PHOTO_DELAY_MS);
    takeAndSavePhoto();
  }
  delay(5);
}

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "esp_camera.h"
#include "ESP32_OV5640_AF.h"   // OV5640 auto-focus library (OV5640 only)

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#include "camera_pins.h"

// Width and height of round display
const int camera_width = 240;
const int camera_height = 240;

TFT_eSPI tft = TFT_eSPI();

// --- OV5640 AF (OV5640 only; fixed-focus modules like OV3660/NT99141 are skipped) ---
OV5640 ov5640 = OV5640();
#define OV5640_FOCUS_TIMEOUT_MS 8000
bool waitForOv5640Focus() {
  const uint32_t started = millis();
  uint8_t status = 0;
  while (millis() - started < OV5640_FOCUS_TIMEOUT_MS) {
    status = ov5640.getFWStatus();
    if (status == 0x10) { Serial.printf("OV5640 focus settled: FW_STATUS=0x%02X\n", status); return true; }
    delay(100);
  }
  Serial.printf("WARN: OV5640 focus not settled; last FW_STATUS=0x%02X\n", status);
  return false;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
//  while(!Serial);

  // Camera pinout
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
//  config.frame_size = FRAMESIZE_UXGA;
  config.frame_size = FRAMESIZE_240X240;
//  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  config.pixel_format = PIXFORMAT_RGB565;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  
  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if(config.pixel_format == PIXFORMAT_JPEG){
    if(psramFound()){
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
    // RGB565 240x240 uses a single internal DRAM buffer: TFT_eSPI's SPI DMA can read DRAM directly; reading from PSRAM crashes pushColors
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  Serial.printf("Camera ready");

  // --- OV5640 Heat Optimization ---
  // 0x302C bit[7:6]: 11=4x (default, hotter) -> 00=1x (weakest, coolest)
  // Own scope; does not affect later AF / set_framesize / set_vflip. OV2640 has no such register and is skipped.
  {
    sensor_t * s = esp_camera_sensor_get();
    if (s && s->id.PID == OV5640_PID) {
      s->set_reg(s, 0x302C, 0xC0, 0x00);
    }
  }

  // --- OV5640 auto-focus init (OV5640 only; fixed-focus modules skipped) ---
  // In RGB565 mode do not switch to SXGA (would overflow the frame buffer); rely on the OV5640 internal full-size focus evaluation
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV5640_PID) {
    Serial.println("OV5640 detected, initializing auto-focus...");
    if (!ov5640.start(s)) {
      Serial.println("WARN: OV5640 CHIPID check failed, AF skipped");
    } else {
      int r1 = ov5640.focusInit();
      int r2 = (r1 == 0) ? ov5640.autoFocusMode() : -1;
      Serial.printf("  focusInit=%d autoFocusMode=%d\n", r1, r2);
      if (r1 == 0 && r2 == 0) waitForOv5640Focus();
      else Serial.println("ERROR: OV5640 AF setup failed");
    }
  } else {
    Serial.printf("PID=0x%04X (not OV5640), AF skipped\n", s ? s->id.PID : 0);
  }

  // Display initialization
  tft.init();
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // turn on backlight
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);

}

void loop() {
  // Obtaining camera images
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(10000);
    return;
  }

  // Decode JPEG images
  uint8_t* buf = fb->buf;
  uint32_t len = fb->len;
  tft.startWrite();
  tft.setAddrWindow(0, 0, camera_width, camera_height);
  tft.pushColors(buf, len);
  tft.endWrite();

  // Release image memory
  esp_camera_fb_return(fb);

  delay(10);
}

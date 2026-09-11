/*
 * This code was adapted from the original, developed by Seeed Studio:
 * https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/#project-i-making-a-handheld-camera
 *
 * Important changes:
 *  - It is not necessary to to cut off J3 on the XIAO ESP32S3 Sense expansion board.
 *  It is possible to use the XIAO's SD Card Reader. For that you need use SD_CS_PIN as 21.
 *
 * - The camera buffer data should be captured as RGB565 (raw image) with a 240x240 frame size
 * to be displayed on the round display. This raw image should be converted to jpeg before save
 * in the SD card. This can be done with the line:
 * bool ok = frame2jpg(fb, 12, &out_buf, &out_len);
 *
 * - The XCLK_FREQ_HZ should be reduced from 20KHz to 10KHz in order to prevent
 * the message "no EV-VSYNC-OVF message" that appears on Serial Monitor (probably due the time
 * added for JPEG conversion.
 *
 * Adapted by MRovai @02June23
 *
 * Generic OV5640 auto-focus patch (OV5640 only; fixed-focus modules like OV3660/NT99141 are skipped):
 *  - After esp_camera_init, run focusInit / autoFocusMode only when PID == OV5640_PID
 *  - In RGB565 mode do not switch to SXGA (would overflow the frame buffer); rely on the OV5640 internal full-size focus evaluation
*/

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "esp_camera.h"
#include "ESP32_OV5640_AF.h"   // OV5640 auto-focus library (github.com/0015/ESP32-OV5640-AF), OV5640 only
#include "FS.h"
#include "SD.h"

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
#define TOUCH_INT D7

/*
 * NOTE: Since the XIAO EPS32S3 Sense is designed with three pull-up resistors
 * R4~R6 connected to the SD card slot, and the round display also has
 * pull-up resistors, the Round Display SD card cannot be read when both
 * are used at the same time. To solve this problem, we need to cut off
 * J3 on the XIAO ESP32S3 Sense expansion board.
 * https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/#preliminary-preparation
 */

#define SD_CS_PIN 21 // ESP32S3 Sense SD Card Reader
//#define SD_CS_PIN D2 // XIAO Round Display SD Card Reader

#include "camera_pins.h"

// Width and height of round display
const int camera_width = 240;
const int camera_height = 240;

// File Counter
int imageCount = 1;
bool camera_sign = false;          // Check camera status
bool sd_sign = false;              // Check sd status

TFT_eSPI tft = TFT_eSPI();

// --- OV5640 AF (OV5640 only) ---
OV5640 ov5640 = OV5640();
#define OV5640_FOCUS_TIMEOUT_MS 8000

// Wait for OV5640 continuous AF to settle (FW_STATUS == 0x10 FOCUSED)
bool waitForOv5640Focus() {
  const uint32_t started = millis();
  uint8_t status = 0;
  while (millis() - started < OV5640_FOCUS_TIMEOUT_MS) {
    status = ov5640.getFWStatus();
    if (status == 0x10) {
      Serial.printf("OV5640 focus settled: FW_STATUS=0x%02X\n", status);
      return true;
    }
    delay(100);
  }
  Serial.printf("WARN: OV5640 focus not settled within %u ms; last FW_STATUS=0x%02X\n",
                OV5640_FOCUS_TIMEOUT_MS, status);
  return false;
}

// SD card write file
void writeFile(fs::FS &fs, const char * path, uint8_t * data, size_t len){
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.write(data, len) == len){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}

bool display_is_pressed(void)
{
    if(digitalRead(TOUCH_INT) != LOW) {
        delay(3);
        if(digitalRead(TOUCH_INT) != LOW)
        return false;
    }
    return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("==== round_display_take_picture_fix ====");

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
  config.xclk_freq_hz = 10000000; // Reduced XCLK_FREQ_HZ from 20KHz to 10KHz (no EV-VSYNC-OVF message)
  config.frame_size = FRAMESIZE_240X240;
  config.pixel_format = PIXFORMAT_RGB565;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  // RGB565 240x240 uses a single internal DRAM buffer: TFT_eSPI's SPI DMA can read DRAM directly;
  // reading from PSRAM crashes pushColors (see the Setup66 + PSRAM issue notes)
  config.fb_location = CAMERA_FB_IN_DRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  Serial.println("Camera ready");
  camera_sign = true; // Camera initialization check passes

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
  // Missing from factory firmware: esp_camera_init does not do AF, so OV5640 defaults to out of focus.
  // In RGB565 mode do not switch to SXGA (would overflow the frame buffer); rely on the OV5640 internal full-size focus evaluation.
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV5640_PID) {
    Serial.println("OV5640 detected, initializing auto-focus...");
    if (!ov5640.start(s)) {
      Serial.println("WARN: OV5640 CHIPID check failed, AF skipped");
    } else {
      int r1 = ov5640.focusInit();                       // upload focus firmware, 0=success
      int r2 = (r1 == 0) ? ov5640.autoFocusMode() : -1;  // enable continuous auto-focus
      Serial.printf("  focusInit=%d autoFocusMode=%d\n", r1, r2);
      if (r1 == 0 && r2 == 0) {
        waitForOv5640Focus();
      } else {
        Serial.println("ERROR: OV5640 AF setup failed; captures will be out of focus.");
      }
    }
  } else {
    Serial.printf("PID=0x%04X (not OV5640), AF skipped - fixed-focus module works as-is.\n",
                  s ? s->id.PID : 0);
  }

  // Display initialization
  tft.init();
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // turn on backlight
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);

  // Initialize SD card - Card Reader under the camera (21)
  // Change to D2 for using Display Card Reader
  if(!SD.begin(SD_CS_PIN)){
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  // Determine if the type of SD card is available
  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  sd_sign = true; // sd initialization check passes
}

void loop() {
  if( sd_sign && camera_sign){

    // Take a photo
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Failed to get camera frame buffer");
      return;
    }

    if(display_is_pressed()){
      Serial.println("display is touched");
      char filename[32];
      sprintf(filename, "/image%d.jpg", imageCount);

      // RGB565 -> JPEG, then save to SD
      size_t out_len = 0;
      uint8_t* out_buf = NULL;
      bool ok = frame2jpg(fb, 12, &out_buf, &out_len);
      if (!ok) {
        Serial.println("JPEG conversion failed");
      } else {
        // Save photo to file
        writeFile(SD, filename, out_buf, out_len);
        Serial.printf("Saved picture: %s\n", filename);
        imageCount++;
        free(out_buf);
      }
    }

    // Draw image on the round display
    uint8_t* buf = fb->buf;
    uint32_t len = fb->len;
    tft.startWrite();
    tft.setAddrWindow(0, 0, camera_width, camera_height);
    tft.pushColors(buf, len);
    tft.endWrite();

    // Release image buffer
    esp_camera_fb_return(fb);

    delay(10);
  }
}

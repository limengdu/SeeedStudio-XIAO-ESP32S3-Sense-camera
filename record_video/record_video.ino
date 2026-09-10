/*
* Since no decoder has been added to this program, the recorded video may not be playable.
* */

#include "esp_camera.h"
#include "ESP32_OV5640_AF.h"   // OV5640 自动对焦库 (仅 OV5640 生效)
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp_timer.h"

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#include "camera_pins.h"

const int SD_PIN_CS = 21;

File videoFile;
bool camera_sign = false;
bool sd_sign = false;
unsigned long lastCaptureTime = 0;
unsigned long captureDuration = 10000; // 10 seconds
int imageCount = 0;

// --- OV5640 AF（仅 OV5640 模组生效，定焦模组自动跳过） ---
OV5640 ov5640 = OV5640();
#define OV5640_AF_FOCUS_FRAMESIZE FRAMESIZE_SXGA  // AF 对焦评估需 >=1280x1024
#define OV5640_FOCUS_TIMEOUT_MS 8000

// 等待 OV5640 连续自动对焦收敛 (FW_STATUS == 0x10 FOCUSED)
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

void setup() {
  Serial.begin(115200);
  while(!Serial);
  
  // Initialize the camera
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
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  camera_sign = true;
  
  // --- OV5640 Heat Optimization ---
  // 0x302C bit[7:6]:11=4×(默认发热)->00=1×(最弱,最不发热)
  // 独立作用域，不影响后续代码；OV2640 无此寄存器自动跳过。
  {
    sensor_t * s = esp_camera_sensor_get();
    if (s && s->id.PID == OV5640_PID) {
      s->set_reg(s, 0x302C, 0xC0, 0x00);
    }
  }

  // --- OV5640 自动对焦初始化（仅 OV5640，定焦模组跳过） ---
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV5640_PID) {
    Serial.println("OV5640 detected, initializing auto-focus...");
    // 切到 SXGA 做对焦（AF 固件需要 >=1280x1024 才能评估对比度）
    if (s->set_framesize(s, OV5640_AF_FOCUS_FRAMESIZE) != 0) {
      Serial.println("WARN: cannot set SXGA for AF focus, captures will be out of focus");
    } else if (!ov5640.start(s)) {
      Serial.println("WARN: OV5640 CHIPID check failed, AF skipped");
      s->set_framesize(s, config.frame_size); // 恢复拍摄分辨率
    } else {
      int r1 = ov5640.focusInit();
      int r2 = (r1 == 0) ? ov5640.autoFocusMode() : -1;
      Serial.printf("  focusInit=%d autoFocusMode=%d\n", r1, r2);
      if (r1 == 0 && r2 == 0 && waitForOv5640Focus()) {
        Serial.println("OV5640 AF ready.");
      } else {
        Serial.println("ERROR: OV5640 AF setup failed; captures will be out of focus.");
      }
      if (s->set_framesize(s, config.frame_size) != 0) {
        Serial.println("WARN: cannot restore capture framesize");
      }
    }
  }

  // Initialize the SD card
  if (!SD.begin(SD_PIN_CS)) {
    Serial.println("SD card initialization failed!");
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
  
  sd_sign = true;

  Serial.println("Video will begin in one minute, please be ready.");
}

void loop() {
  // Camera & SD available, start taking video
  if (camera_sign && sd_sign) {
    // Get the current time
    unsigned long now = millis();

    //If it has been more than 1 minute since the last video capture, start capturing a new video
    if ((now - lastCaptureTime) >= 60000) {
      char filename[32];
      sprintf(filename, "/video%d.avi", imageCount);
      videoFile = SD.open(filename, FILE_WRITE);
      if (!videoFile) {
        Serial.println("Error opening video file!");
        return;
      }
      Serial.printf("Recording video: %s\n", filename);
      lastCaptureTime = now;
      
      // Start capturing video frames
      while ((millis() - lastCaptureTime) < captureDuration) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("Error getting framebuffer!");
          break;
        }
        videoFile.write(fb->buf, fb->len);
        esp_camera_fb_return(fb);
      }
      
      // Close the video file
      videoFile.close();
      Serial.printf("Video saved: %s\n", filename);
      imageCount++;

      Serial.println("Video will begin in one minute, please be ready.");

      // Wait for the remaining time of the minute
      delay(60000 - (millis() - lastCaptureTime));
    }
  }
}

This routine needs to be used in conjunction with [Round Display for XIAO](https://www.seeedstudio.com/Seeed-Studio-Round-Display-for-XIAO-p-5638.html), the main purpose of which is to display the live XIAO ESP32S3 Sense screen on the screen. And save the current screen to SD card when touching the screen.

## XIAO Round Display Software Preparation

- First, you need to search and download the latest version `TFT_eSPI` and `LVGL` libraries in the Arduino IDE Library Manager.
 - The TFT_eSPI library compatible with Round Display has been submitted for a merge request, so when the next version is released, you can search and download TFT_eSPI in Arduino IDE to use it normally. Until then, if you need to use the TFT_eSPI library for Round Display, please download it from: https://github.com/Maxwelltoo/TFT_eSPI
- Then, we also need to download and import the configuration library for Round Display.
https://github.com/Seeed-Studio/Seeed_Arduino_RoundDisplay
- Take the lv_conf.h file and place it to the root directory of the Arduino library.
- Comment out the line `#include <User_Setup.h>` and uncomment the line `#include <User_Setups/Setup66_Seeed_XIAO_RoundDisplay.h>` in the `User_Setup_Select.h` file.




## Capure of images to save as a Dataset on the SD Card Reader
This code was adapted from the original, developed by Seeed Studio: 
 - https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/#project-i-making-a-handheld-camera

**Important changes:**

- It is not necessary to to cut off J3 on the XIAO ESP32S3 Sense expansion board. It is possible to use the XIAO's SD Card Reader. For that you need use `SD_CS_PIN` as 21.
- The camera buffer data should be captured as RGB565 (raw image) with a 240x240 frame size  to be displayed on the round display. This raw image should be converted to jpeg before save in the SD card. This can be done with the line: 
  - `esp_err_t ret = frame2jpg(fb, 12, &out_buf, &out_len)` 
- The `XCLK_FREQ_HZ` should be reduced from 20KHz to 10KHz in order to prevent the message "no EV-VSYNC-OVF message" that appears on Serial Monitor (probably due the time added for JPEG conversion. 

Adapted by MRovai @02June23
From: https://github.com/Mjrovai/XIAO-ESP32S3-Sense/blob/main/camera_round_display_save_jpeg


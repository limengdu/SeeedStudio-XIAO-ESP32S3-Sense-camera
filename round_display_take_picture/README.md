This routine needs to be used in conjunction with [Round Display for XIAO](https://www.seeedstudio.com/Seeed-Studio-Round-Display-for-XIAO-p-5638.html), the main purpose of which is to display the live XIAO ESP32S3 Sense screen on the screen. And save the current screen to SD card when touching the screen.

Since the XIAO EPS32S3 Sense is designed with three pull-up resistors R4~R6 connected to the SD card slot, and the round display also has pull-up resistors, the SD card cannot be read when both are used at the same time. To solve this problem, we need to cut off J3 on the XIAO ESP32S3 Sense expansion board.

However, we need to thank engineer Mjrovai for the new method of using the microSD card slot on the XIAO ESP32S3 Sense at the same time, which is also possible at software level. We can refer to his [methods and procedures](https://github.com/Mjrovai/XIAO-ESP32S3-Sense/tree/main/camera_round_display_save_jpeg).

https://github.com/limengdu/SeeedStudio-XIAO-ESP32S3-Sense-camera/tree/main/round_display_take_picture_fix








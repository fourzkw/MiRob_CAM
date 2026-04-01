#ifndef CONFIG_H
#define CONFIG_H

// Wi-Fi AP settings
#define WIFI_AP_SSID        "MiRob_CAM"
#define WIFI_AP_PASSWORD    "12345678"
#define WIFI_AP_CHANNEL     6
#define WIFI_AP_MAX_CONN    4

// Wi-Fi STA settings
// Leave WIFI_STA_SSID empty ("") to disable STA connection.
#define WIFI_STA_SSID       "fourzkw"
#define WIFI_STA_PASSWORD   "11111111"
#define WIFI_STA_TIMEOUT_MS 15000

// Photo capture button (press to capture)
#define PIN_BUTTON          13
#define BUTTON_ACTIVE_LOW   1

// Mode toggle button on GPIO12 (press to switch mode 1 <-> 2)
#define PIN_MODE_BUTTON        12
#define MODE_BUTTON_ACTIVE_LOW 1

// Mode indicator LED on GPIO33 (active LOW: LOW = ON, HIGH = OFF)
#define PIN_MODE_LED        33

// TF card and photo storage
#define SD_MMC_1BIT         1
#define PHOTO_DIR           "/photos"
#define PHOTO_PREFIX        "img_"
#define PHOTO_EXT           ".jpg"
// JPEG quality for saved photos (0-63, smaller = higher quality)
// 提高仅拍照模式下的成像质量，这里采用更高质量的压缩参数。
#define PHOTO_QUALITY       20
#define CAPTURE_MIN_INTERVAL_MS 1200

// Camera clock
#define CAM_XCLK_FREQ       20000000

// Device mode
// 1 = stream + photo, 2 = photo-only
#define DEFAULT_DEVICE_MODE 1

#endif

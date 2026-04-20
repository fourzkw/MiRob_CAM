#ifndef CONFIG_H
#define CONFIG_H

#include <esp_camera.h>

// Mode switch button (short press toggles mode)
#define PIN_MODE_BUTTON                     3
#define MODE_BUTTON_ACTIVE_LOW              0
#define MODE_BUTTON_USE_INTERNAL_PULLUP     1

// Capture button (short press captures in mode1/mode2/mode3/mode5)
#define PIN_CAPTURE_BUTTON                  21
#define CAPTURE_BUTTON_ACTIVE_LOW           0
#define CAPTURE_BUTTON_USE_INTERNAL_PULLUP  1

// WS2812 mode indicator
#define PIN_MODE_WS2812                     20
#define MODE_WS2812_LED_COUNT               1
#define MODE_WS2812_BRIGHTNESS              24
#define RECORD_SIGNAL_BLINK_MS              180
#define RECORD_SIGNAL_COLOR_R               255
#define RECORD_SIGNAL_COLOR_G               0
#define RECORD_SIGNAL_COLOR_B               0

// Fill light (on during capture)
#define PIN_FILL_LIGHT                      48
#define FILL_LIGHT_ACTIVE                   HIGH
#define FILL_LIGHT_WARMUP_MS                80

// Button debounce
#define CAPTURE_BUTTON_DEBOUNCE_MS          30

// TF card and photo storage
#define SD_MMC_1BIT                         0
#define PHOTO_DIR                           "/photos"
#define PHOTO_PREFIX                        "img_"
#define PHOTO_EXT                           ".jpg"
#define VIDEO_DIR                           "/videos"
#define VIDEO_PREFIX                        "vid_"
#define VIDEO_EXT                           ".avi"

// Capture flow control
#define CAPTURE_MIN_INTERVAL_MS             500
#define SD_REINIT_INTERVAL_MS               5000
#define CAPTURE_DISCARD_FRAMES_BEFORE_SAVE  1
#define CAPTURE_DISCARD_FRAME_DELAY_MS      40
#define VIDEO_FPS                           8
#define VIDEO_DURATION_MS                   6000
#define VIDEO_DISCARD_FRAMES_BEFORE_SAVE    1
#define VIDEO_DISCARD_FRAME_DELAY_MS        25
#define VIDEO_USE_FILL_LIGHT                0
#define VIDEO_FILL_LIGHT_WARMUP_MS          60

// Camera sensor select
// Set CAM_SENSOR_TYPE to CAM_SENSOR_OV3660 or CAM_SENSOR_OV5640.
#define CAM_SENSOR_OV3660                   3660
#define CAM_SENSOR_OV5640                   5640
#define CAM_SENSOR_TYPE                     CAM_SENSOR_OV3660

// Photo/record profile (mode1/mode2/mode3/mode5 capture) - independent per sensor
#if (CAM_SENSOR_TYPE == CAM_SENSOR_OV3660)
#define CAMERA_MODEL_NAME                   "OV3660"
#define CAM_PHOTO_FRAME_SIZE                FRAMESIZE_UXGA
#define CAM_PHOTO_QUALITY                   8
#define CAM_RECORD_FRAME_SIZE               FRAMESIZE_SVGA
#define CAM_RECORD_QUALITY                  12
#define CAM_AEC_VALUE                       150
#define CAM_AGC_GAIN                        4
#elif (CAM_SENSOR_TYPE == CAM_SENSOR_OV5640)
#define CAMERA_MODEL_NAME                   "OV5640"
#define CAM_PHOTO_FRAME_SIZE                FRAMESIZE_UXGA
#define CAM_PHOTO_QUALITY                   10
#define CAM_RECORD_FRAME_SIZE               FRAMESIZE_XGA
#define CAM_RECORD_QUALITY                  12
#define CAM_AEC_VALUE                       220
#define CAM_AGC_GAIN                        4
#else
#error "Unsupported CAM_SENSOR_TYPE. Use CAM_SENSOR_OV3660 or CAM_SENSOR_OV5640."
#endif

// Preview profile (mode4: low resolution web stream)
#define CAM_PREVIEW_FRAME_SIZE              FRAMESIZE_QVGA
#define CAM_PREVIEW_QUALITY                 12
#define CAM_PREVIEW_FRAME_DELAY_MS          90

// TFT live view: JPEG QVGA (decoded to RGB565 on display); QVGA center-cropped to 240x240
#define CAM_TFT_LIVE_FRAME_SIZE             FRAMESIZE_QVGA
#define TFT_LIVE_REFRESH_MIN_MS             40
// 拍照成功后 TFT 全屏显示刚保存的 JPEG，持续时间（毫秒）后恢复实时预览。
#define TFT_CAPTURE_REVIEW_HOLD_MS          2000
#define TFT_CAPTURE_REVIEW_KEEPALIVE_MS     160

// Backward compatibility alias (old name)
#define PHOTO_QUALITY                       CAM_PHOTO_QUALITY

// Manual AE/AGC tuning for fixed capture modes (mode1/mode2). AWB stays auto.
#define LOCK_AE_AWB                         1

// Camera clock
// Use a conservative XCLK to improve sensor init stability.
#define CAM_XCLK_FREQ                       10000000

// Mode4: MQTT preview — connect to WiFi (STA) and publish JPEG frames to broker.
#define MQTT_WIFI_SSID                    "YOUR_WIFI_SSID"
#define MQTT_WIFI_PASSWORD                "YOUR_WIFI_PASSWORD"
#define MQTT_WIFI_CONNECT_TIMEOUT_MS        20000

#define MQTT_BROKER_HOST                  "192.168.1.100"
#define MQTT_BROKER_PORT                  1883
#define MQTT_CLIENT_ID                    "mirob_cam"
#define MQTT_USER                         ""
#define MQTT_PASSWORD                     ""
#define MQTT_IMAGE_TOPIC                  "mirob/cam/image"

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE              65535
#endif
#define MQTT_MAX_PUBLISH_BYTES            (MQTT_MAX_PACKET_SIZE - 256)

// TFT screen (SPI)
#define TFT_SCL_PIN                         18
#define TFT_SDA_PIN                         19
#define TFT_DC_PIN                          10
#define TFT_CS_PIN                          -1
#define TFT_RST_PIN                         47
#define TFT_BL_PIN                          -1
#define TFT_BL_ACTIVE                       HIGH
#define TFT_WIDTH                           240
#define TFT_HEIGHT                          240
#define TFT_ROTATION                        3
#define TFT_SPI_MODE                        SPI_MODE3
#define TFT_SPI_HZ                          1000000UL
// If live colors look wrong (red/blue swapped), toggle to 0.
#define TFT_RGB565_BIG_ENDIAN               1

// TFT 纯色 JPEG 测试：用 RGB888 填图 → fmt2jpg → jpg2rgb565 → 全屏，验证 JPEG 编解码与屏幕链路。
// 1 = 上电进入测试（不取摄像头预览帧）；也可用串口命令 tfttest / tfttest off 切换。
#define TFT_JPEG_SOLID_TEST_AT_BOOT         0
#define TFT_JPEG_SOLID_TEST_INTERVAL_MS     1000
#define TFT_JPEG_SOLID_TEST_JPEG_QUALITY    10
#define TFT_JPEG_SOLID_TEST_WIDTH           320
#define TFT_JPEG_SOLID_TEST_HEIGHT          240

#endif

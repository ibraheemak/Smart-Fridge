//user defined parameters

// OLED display width and height, in pixels
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

//  4x3 keypad GPIO pins
#define R4   19 
#define R3   13 
#define R2   12 
#define R1   4    
#define C1   21 
#define C2   27 
#define C3   33  

// INMP441 I2S microphone GPIO pins
#define I2S_WS 14
#define I2S_SD 15
#define I2S_SCK 32
#define I2S_PORT I2S_NUM_0

// external DAC MAX98357A GPIO pins
#define DAC_BCK_PIN 26
#define DAC_WS_PIN 25
#define DAC_DATA_PIN 22

// ============================================================================
// ESP32-CAM CAMERA CONFIGURATION (AI Thinker ESP32-CAM)
// ============================================================================
// Camera pin definitions for AI Thinker ESP32-CAM board
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
#define FLASH_GPIO_NUM     4

// ============================================================================
// DEBUG SETTINGS
// ============================================================================
#define DEBUG_MODE 1                // Set to 1 to enable debug output and Gemini text description

// ============================================================================
// CAMERA CAPTURE SETTINGS
// ============================================================================
#define CAMERA_JPEG_QUALITY 20      // 0-63, lower is better quality
#define CAMERA_XCLK_FREQ 5000000    // XClk frequency reduced to 5MHz (5000000Hz) to avoid LEDC timer conflicts
#define CAMERA_LEDC_CHANNEL 1        // Use LEDC_CHANNEL_1 instead of CHANNEL_0 to avoid conflicts
#define FLASH_DURATION_MS 600       // Time (ms) for AEC to converge after flushing pre-flash frames
#define FLASH_PWM_DUTY      80      // Flash brightness: 0 (off) – 255 (full). 80 ≈ 31%
#define CAPTURE_INTERVAL_MS 30000   // Interval between captures (30 seconds)
#define CAMERA_WARMUP_DELAY_MS 500  // Warmup delay before actual capture

// NOTE: GPIO CONFLICTS - Your board shares pins between peripherals:
//       Keypad (R4=19, Y4=19 conflict), I2S/DAC (25,26,27 overlap with camera)
//       If camera still fails, disable unused peripherals or remap their pins.

// ============================================================================
// WiFi SETTINGS
// ============================================================================
#define WIFI_MAX_ATTEMPTS 20        // Maximum WiFi connection attempts
#define WIFI_TIMEOUT_MS 10000       // WiFi connection timeout

// WiFiManager captive-portal settings
#define WIFI_AP_NAME          "SmartFridge_Setup"  // AP name shown during setup
#define WIFI_PORTAL_TIMEOUT_S 180                  // Seconds before portal gives up and restarts
#define RESET_BUTTON_PIN      0                    // GPIO 0 = BOOT button (hold at power-on to wipe credentials)
#define RESET_HOLD_MS         3000                 // How long to hold the button to trigger a wipe

// ============================================================================
// GEMINI API SETTINGS
// ============================================================================
#define GEMINI_REQUEST_TIMEOUT_MS 30000  // Request timeout (30 seconds)
#define GEMINI_MAX_RETRIES 2             // Number of retry attempts
#define GEMINI_DEBUG_DESCRIPTION 1       // Set to 1 to include text description in debug mode

// ============================================================================
// FIREBASE SETTINGS
// ============================================================================
#define FIREBASE_REQUEST_TIMEOUT_MS 15000  // Firebase request timeout

// ============================================================================
// TIMEZONE SETTINGS
// ============================================================================
// POSIX timezone string — controls local time for timestamps and document IDs.
// Israel: "IST-2IDT,M3.4.4/26,M10.5.0"  (UTC+2, DST UTC+3)
// UTC:    "UTC0"
#define TIMEZONE "IST-2IDT,M3.4.4/26,M10.5.0"

// ============================================================================
// LED STRIP SETTINGS (WS2811B addressable)
// ============================================================================
// GPIO 14 is free on AI Thinker ESP32-CAM (not used by camera or critical boot path)
#define LED_DATA_PIN    14
#define LED_NUM_LEDS    4    
#define LED_BRIGHTNESS  200   // 0-255




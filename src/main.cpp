// Lecteur Wav/Mp3/m4a Avec des signaux UDP
// Dependance : https://github.com/schreibfaul1/ESP32-audioI2S
//
// La carte Sd doit etre formatée soit en FAT16 soit en FAT32
// pour le format FAT32, il faut formatter la carte sd avec cet outil:
// https://www.sdcard.org/downloads/formatter/
//
// Les fichiers audios sont à mettre à la racine de la carte SD
//
// Les Signaux UDP possibles sont :
//
// Track : "0" à nombre de tracks audios
//
// Volume : "V 0" à "V 255"
//
// Pause/Resume : "P" pour alterner en pause et reprendre la lecteur,
// "P 0" pour mettre en pause, "P 1" pour reprendre la lecture.
//
// Loop file : "L" pour alterner en looper et ne pas looper,
// "L 0" pour ne pas looper, "L 1" pour looper.
//
// Balance : "B -16" à "B 16"
//
// Jump : "J 60" pour aller à un endroit specifique dans la track,
// 60 = 60 secondes
//
// Tonality : "T -40 0 6" fonctionne comme un equilibreur,
// le premier nombre est le gain pour les graves,
// le deuxieme pour les médiums et le troisieme pour les aigus,
// le gain va de -40 à 6 (en dB)

#include "Arduino.h"
#include "WiFi.h"
#include "jsonParser.h"
#include "ESPAsyncWebServer.h"
#include <DNSServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"
#include "sstream"
#include <Adafruit_NeoPixel.h>

#include "BluetoothA2DPSink.h"
#include "PCF8575.h"
#include "esp32-hal-adc.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "driver/i2s.h"

//  adjust addresses if needed
PCF8575 PCF(0x20);
// Memorize last read value of PCF pins (1 = HIGH, 0 = LOW)
static uint16_t pcfPrevValue = 0xFFFF;

// branchement Carte SD
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

// Branchement Amplificateur I2S
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC 26
#define I2S_ENABLE 17
// Buttons to adjust audio balance
#define BTN_INC_BAL 14 // increase balance (right)
#define BTN_DEC_BAL 16 // decrease balance (left)
#define BTN_PLAY_PAUSE 33 // toggle play/pause
#define PCF_INT_PIN 15

#define PCF_PULL_STATE LOW

#define CHRG_STATUS 13
#define LED_IO4 4

#define USB_VOLTAGE_PIN 32
//Not working on Rev1
#define BATTERY_VOLTAGE_PIN 34 // use ADC1 pin to avoid Wi-Fi blocking ADC2

String ssid = "punkhazard";
String password = "00000000";
// String ssid = "SFR_B4C8";                 // nom du routeur
// String ssid = "Livebox-75C0";                 // nom du routeur
// String ssid = "Bbox-7A159A77-2.4G";     // nom du routeur
// String password = "UxWygsEU44zhs3ynNG"; // mot de passe
// String password = "enorksenez3vesterish"; // mot de passe
// String password = "ipW2j3EzJQg6LF9Er6"; // mot de passe

IPAddress ip(192, 168, 0, 104);    // Local IP (static)
IPAddress gateway(192, 168, 0, 2); // Router IP
unsigned int localPort = 8266;     // port de reception UDP
IPAddress subnet(255, 255, 255, 0);

bool loop_file = true;               // Default loop audio files
const bool REQUEST_STATIC_IP = true; // Demander l'attribution d'une ip statique
bool auto_play = false;              // Lit la premiere track au demarrage
const bool DEBUG = true;             // Afficher les messages dans la console
namespace patch
{
    template <typename T>
    std::string to_string(const T &n)
    {
        std::ostringstream stm;
        stm << n;
        return stm.str();
    }
}
typedef struct s_music_data
{
    String path;
    uint8_t index;
} t_music_data;

String note = "";
uint8_t volume;
std::vector<t_music_data> music_data;
std::vector<String> files_list;
Audio audio;
BluetoothA2DPSink a2dp;
WiFiUDP udp;
AsyncWebServer server(80);
DNSServer dnsServer;
// NeoPixel (WS2812) on IO4, 4 LEDs
static const uint8_t NEO_PIN = LED_IO4;
static const uint16_t NEO_COUNT = 4;
static Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
static uint16_t rainbowOffset = 0;
static uint32_t lastRainbowMs = 0;

// Forward declarations for globals used by LED battery indicator
extern float usbVoltage;
extern float batteryVoltage;
// Define globals early so they are visible to functions below in some toolchains
float usbVoltage = 0;
float batteryVoltage = 0;
// Bluetooth connection state (used to show pairing rainbow when not connected)
static bool a2dpConnected = false;

static uint32_t colorWheel(uint8_t pos)
{
    pos = 255 - pos;
    if (pos < 85)
    {
        return strip.Color(255 - pos * 3, 0, pos * 3);
    }
    if (pos < 170)
    {
        pos -= 85;
        return strip.Color(0, pos * 3, 255 - pos * 3);
    }
    pos -= 170;
    return strip.Color(pos * 3, 255 - pos * 3, 0);
}

static void updateRainbow(uint32_t nowMs)
{
    const uint32_t intervalMs = 5; // ~50 FPS
    if (nowMs - lastRainbowMs < intervalMs) return;
    lastRainbowMs = nowMs;
    for (uint16_t i = 0; i < NEO_COUNT; ++i)
    {
        uint8_t wheelPos = (uint8_t)(((i * 64) / NEO_COUNT + rainbowOffset) & 0xFF);
        strip.setPixelColor(i, colorWheel(wheelPos));
    }
    strip.show();
    rainbowOffset++;
}
static void updateBatteryLeds(uint32_t nowMs, float usbV, float batV, bool isCharging)
{
    const uint32_t updateIntervalMs = 100; // refresh at ~10 Hz
    static uint32_t lastUpdateMs = 0;
    if (nowMs - lastUpdateMs < updateIntervalMs) return;
    lastUpdateMs = nowMs;

    // If charger is connected and not charging anymore, show "fully charged" in blue
    bool isUsbPresent = (usbV > 4.0f);
    if (isUsbPresent && !isCharging)
    {
        const uint32_t blue = strip.Color(0, 0, 150);
        for (uint16_t i = 0; i < NEO_COUNT; ++i)
        {
            strip.setPixelColor(i, blue);
        }
        strip.show();
        return;
    }

    // Map measured battery voltage to percentage using simple 1S LiPo range
    // Adjust vMin/vMax if your chemistry differs
    float v = batV;
    const float vMin = 3.3f;  // empty
    const float vMax = 4.2f;  // full
    if (v < vMin) v = vMin;
    if (v > vMax) v = vMax;
    float pct = (v - vMin) / (vMax - vMin); // 0..1
    int greenCount = (int)(pct * NEO_COUNT + 0.5f); // round to nearest LED
    if (greenCount < 0) greenCount = 0;
    if (greenCount > (int)NEO_COUNT) greenCount = NEO_COUNT;

    static bool blinkOn = false;
    static uint32_t lastBlinkMs = 0;
    const uint32_t blinkIntervalMs = 500;
    if (isCharging)
    {
        if (nowMs - lastBlinkMs >= blinkIntervalMs)
        {
            lastBlinkMs = nowMs;
            blinkOn = !blinkOn;
        }
    }
    else
    {
        blinkOn = true; // show steady when not charging
    }

    // Colors
    const uint32_t green = strip.Color(0, 150, 0);
    const uint32_t red   = strip.Color(200, 0, 0);
    const uint32_t off   = strip.Color(0, 0, 0);

    for (uint16_t i = 0; i < NEO_COUNT; ++i)
    {
        if ((int)i < greenCount)
        {
            // Last lit green LED blinks when charging
            if (isCharging && (int)i == greenCount - 1 && greenCount > 0)
            {
                strip.setPixelColor(i, blinkOn ? green : off);
            }
            else
            {
                strip.setPixelColor(i, green);
            }
        }
        else
        {
            strip.setPixelColor(i, red);
        }
    }
    strip.show();
}
// Balance control state
int8_t currentBalance = 0;               // valid range: -16 (left) .. +16 (right)
bool bt_mode = true;                     // Bluetooth speaker mode (BT-only firmware)
bool prevBtnIncPressed = true;           // using INPUT_PULLUP, idle is HIGH
bool prevBtnDecPressed = true;           // using INPUT_PULLUP, idle is HIGH
unsigned long lastBalanceButtonMs = 0;   // debounce timer
const unsigned long balanceDebounceMs = 200;

// FreeRTOS primitives for button handling via ISR
typedef enum { BTN_EVT_INC = 1, BTN_EVT_DEC = 2, BTN_EVT_PLAY_PAUSE = 3, BTN_EVT_PCF_INT = 4 } ButtonEvent;
static QueueHandle_t buttonQueue = NULL;
static TaskHandle_t buttonTaskHandle = NULL;
// Queue to request track playback from PCF events (handled in main loop)
static QueueHandle_t pcfPlayQueue = NULL;

// Adjust these to your resistor divider ratios: Vreal = Vadcpin * GAIN
// Example: two equal resistors -> GAIN = 2.0f
static const float USB_DIVIDER_GAIN = 2.0f;
static const float BATTERY_DIVIDER_GAIN = 2.0f;

// Forward declaration for persistence helper
void update_spiffs();

// =========================
// BT-only: Button handling
// =========================
#define DEBUG_BT false
// Buttons IO Attribution (BT control)
#define START_BUTTON GPIO_NUM_33
#define DOWN_BUTTON 22
#define UP_BUTTON 23

volatile bool is_playing = false;
// Volume modifier (0-14)
int16_t current_volume = 14;
// Buttons Requests/tasks
TaskHandle_t StartPressTaskHandle = NULL;
TaskHandle_t StartPressShortTaskHandle = NULL;
TaskHandle_t DownPressTaskHandle = NULL;
TaskHandle_t DownPressShortTaskHandle = NULL;
TaskHandle_t UpPressTaskHandle = NULL;
TaskHandle_t UpPressShortTaskHandle = NULL;
// Buttons press timing
volatile bool isStartPressed = false;
volatile uint32_t startPressedTime = 0;
volatile bool isDownPressed = false;
volatile uint32_t downPressedTime = 0;
volatile bool isUpPressed = false;
volatile uint32_t upPressedTime = 0;
// Local buttons on IO14/IO16 (map to Up/Down behavior)
volatile bool isIncPressed = false;
volatile uint32_t incPressedTime = 0;
volatile bool isDecPressed = false;
volatile uint32_t decPressedTime = 0;

// Optional prompt sounds (define to enable)
// #define INCLUDE_PROMPTS 0
#ifdef INCLUDE_PROMPTS
extern const uint8_t PROGMEM bike_groove_on_wav[];
extern const uint32_t bike_groove_on_wav_len;
extern const uint8_t PROGMEM bike_groove_off_wav[];
extern const uint32_t bike_groove_off_wav_len;
extern const uint8_t PROGMEM bluetooth_connected_wav[];
extern const uint32_t bluetooth_connected_wav_len;
extern const uint8_t PROGMEM bluetooth_disconnected_wav[];
extern const uint32_t bluetooth_disconnected_wav_len;
static const uint32_t chunkSize = 60000;
static uint8_t audio_ble_wav_ram[chunkSize];
static void readSound(const uint8_t PROGMEM sound[], uint32_t sound_len)
{
    unsigned int totalLength = sound_len;
    unsigned int offset = 0;
    while (totalLength > 0)
    {
        unsigned int currentChunkSize = min(chunkSize, totalLength);
        for (unsigned int i = 0; i < currentChunkSize; i++)
        {
            audio_ble_wav_ram[i] = pgm_read_byte(&sound[offset + i]);
        }
        a2dp.audio_data_callback((uint8_t *)audio_ble_wav_ram, currentChunkSize);
        offset += currentChunkSize;
        totalLength -= currentChunkSize;
    }
}
#endif

static void onBluetoothConnect2(esp_a2d_connection_state_t state, void *)
{
#ifdef INCLUDE_PROMPTS
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED)
    {
        a2dpConnected = true;
        if (DEBUG_BT) Serial.println("Bluetooth connected");
        readSound(bluetooth_connected_wav, bluetooth_connected_wav_len);
    }
    if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
    {
        a2dpConnected = false;
        if (DEBUG_BT) Serial.println("Bluetooth disconnected");
        readSound(bluetooth_disconnected_wav, bluetooth_disconnected_wav_len);
    }
#else
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) a2dpConnected = true;
    if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) a2dpConnected = false;
#endif
}

static void callbackaudio(esp_a2d_audio_state_t state, void* /*param*/)
{
    if (DEBUG_BT)
    {
        Serial.printf("Callback state:%d\n", state);
    }
    is_playing = (state == ESP_A2D_AUDIO_STATE_STARTED);
}

static void StartPressTask(void *parameter)
{
    if (DEBUG_BT) Serial.println("Start Button Long Press Detected (Task)");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
#ifdef INCLUDE_PROMPTS
    readSound(bike_groove_off_wav, bike_groove_off_wav_len);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
#endif
    digitalWrite(I2S_ENABLE, LOW);
    // Turn off NeoPixel LEDs
    strip.clear();
    strip.setBrightness(0);
    strip.show();
    // Wait for button release to avoid immediate wake from EXT0 (active-low)
    while (digitalRead((int)START_BUTTON) == LOW)
    {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
    esp_deep_sleep_start();
    StartPressTaskHandle = NULL;
    vTaskDelete(NULL);
}

static void StartPressShortTask(void *parameter)
{
    if (DEBUG_BT)
    {
        Serial.printf("Start/Stop\n");
    }
    if (is_playing)
    {
        a2dp.stop();
        is_playing = false;
    }
    else
    {
        a2dp.play();
        is_playing = true;
    }
    vTaskDelete(NULL);
}

void IRAM_ATTR StartPress()
{
    if (digitalRead((int)START_BUTTON) == LOW)
    {
        isStartPressed = true;
        startPressedTime = millis();
        if (StartPressTaskHandle == NULL)
        {
            xTaskCreate(StartPressTask, "StartPressTask", 2048, NULL, 1, &StartPressTaskHandle);
        }
    }
    else if (isStartPressed == true)
    {
        isStartPressed = false;
        if (millis() - startPressedTime < 2000)
        {
            if (StartPressTaskHandle != NULL)
            {
                vTaskDelete(StartPressTaskHandle);
                StartPressTaskHandle = NULL;
            }
            xTaskCreate(StartPressShortTask, "StartPressShortTask", 2048, NULL, 1, &StartPressShortTaskHandle);
        }
    }
}

static void DownPressTask(void *parameter)
{
    if (DEBUG_BT) Serial.println("Down Button Long Press (Task)");
    vTaskDelay(700 / portTICK_PERIOD_MS);
    a2dp.previous();
    DownPressTaskHandle = NULL;
    vTaskDelete(NULL);
}

static void DownPressShortTask(void *parameter)
{
    int v = (int)a2dp.get_volume();
    v = v - 8;
    if (v < 0) v = 0;
    if (DEBUG_BT) Serial.printf("[BT] Volume down -> %d\n", v);
    a2dp.set_volume((uint8_t)v);
    vTaskDelete(NULL);
}

void IRAM_ATTR DownPress()
{
    if (digitalRead(DOWN_BUTTON) == LOW)
    {
        isDownPressed = true;
        downPressedTime = millis();
        if (DownPressTaskHandle == NULL)
        {
            xTaskCreate(DownPressTask, "DownPressTask", 2048, NULL, 1, &DownPressTaskHandle);
        }
    }
    else
    {
        isDownPressed = false;
        if (millis() - downPressedTime < 1000)
        {
            if (DownPressTaskHandle != NULL)
            {
                vTaskDelete(DownPressTaskHandle);
                DownPressTaskHandle = NULL;
            }
            xTaskCreate(DownPressShortTask, "DownPressShortTask", 2048, NULL, 1, &DownPressShortTaskHandle);
        }
    }
}

static void UpPressTask(void *parameter)
{
    if (DEBUG_BT) Serial.println("Up Button Long Press (Task)");
    vTaskDelay(700 / portTICK_PERIOD_MS);
    a2dp.next();
    UpPressTaskHandle = NULL;
    vTaskDelete(NULL);
}

static void UpPressShortTask(void *parameter)
{
    int v = (int)a2dp.get_volume();
    v = v + 8;
    if (v > 127) v = 127;
    if (DEBUG_BT) Serial.printf("[BT] Volume up -> %d\n", v);
    a2dp.set_volume((uint8_t)v);
    vTaskDelete(NULL);
}

void IRAM_ATTR UpPress()
{
    if (digitalRead(UP_BUTTON) == LOW)
    {
        isUpPressed = true;
        upPressedTime = millis();
        if (UpPressTaskHandle == NULL)
        {
            xTaskCreate(UpPressTask, "UpPressTask", 2048, NULL, 1, &UpPressTaskHandle);
        }
    }
    else
    {
        isUpPressed = false;
        if (millis() - upPressedTime < 1000)
        {
            if (UpPressTaskHandle != NULL)
            {
                vTaskDelete(UpPressTaskHandle);
                UpPressTaskHandle = NULL;
            }
            xTaskCreate(UpPressShortTask, "UpPressShortTask", 2048, NULL, 1, &UpPressShortTaskHandle);
        }
    }
}

// Map IO14 (BTN_INC_BAL) to UpPress* behavior (short: volume up, long: next)
void IRAM_ATTR BtnInc()
{
    if (digitalRead(BTN_INC_BAL) == LOW)
    {
        isIncPressed = true;
        incPressedTime = millis();
        if (UpPressTaskHandle == NULL)
        {
            xTaskCreate(UpPressTask, "UpPressTask", 2048, NULL, 1, &UpPressTaskHandle);
        }
    }
    else
    {
        isIncPressed = false;
        if (millis() - incPressedTime < 700)
        {
            if (UpPressTaskHandle != NULL)
            {
                vTaskDelete(UpPressTaskHandle);
                UpPressTaskHandle = NULL;
            }
            xTaskCreate(UpPressShortTask, "UpPressShortTask", 2048, NULL, 1, &UpPressShortTaskHandle);
        }
    }
}

// Map IO16 (BTN_DEC_BAL) to DownPress* behavior (short: volume down, long: previous)
void IRAM_ATTR BtnDec()
{
    if (digitalRead(BTN_DEC_BAL) == LOW)
    {
        isDecPressed = true;
        decPressedTime = millis();
        if (DownPressTaskHandle == NULL)
        {
            xTaskCreate(DownPressTask, "DownPressTask", 2048, NULL, 1, &DownPressTaskHandle);
        }
    }
    else
    {
        isDecPressed = false;
        if (millis() - decPressedTime < 700)
        {
            if (DownPressTaskHandle != NULL)
            {
                vTaskDelete(DownPressTaskHandle);
                DownPressTaskHandle = NULL;
            }
            xTaskCreate(DownPressShortTask, "DownPressShortTask", 2048, NULL, 1, &DownPressShortTaskHandle);
        }
    }
}

static void startBtMode()
{
    // Stop any file playback and start A2DP sink on same I2S pins
    audio.stopSong();
    // Ensure no previous I2S driver is active (Audio library or others)
    i2s_driver_uninstall(I2S_NUM_0);
    i2s_pin_config_t pins = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    a2dp.set_pin_config(pins);
    a2dp.set_on_audio_state_changed(&callbackaudio);
    a2dp.set_on_connection_state_changed(onBluetoothConnect2);
    a2dp.set_auto_reconnect(true);
    a2dp.start("Enceinte Cuisine");
    a2dp.set_volume(current_volume * 8 + 15);
#ifdef INCLUDE_PROMPTS
    readSound(bike_groove_on_wav, bike_groove_on_wav_len);
#endif
    Serial.println("[BT] A2DP sink started");
    a2dpConnected = false; // start in pairing mode until connection is established
}

static void stopBtMode()
{
    a2dp.stop();
    Serial.println("[BT] A2DP sink stopped");
}

static void playTrackIndex(uint8_t trackIndex)
{
    if (bt_mode)
    {
        stopBtMode();
        bt_mode = false;
        update_spiffs();
    }
    if (files_list.size() > trackIndex)
    {
        Serial.printf("[PCF8575] Playing track %u: %s\n", trackIndex, files_list[trackIndex].c_str());
        audio.connecttoFS(SD, files_list[trackIndex].c_str());
    }
    else
    {
        Serial.printf("[PCF8575] Track %u out of range (files: %u)\n", trackIndex, (unsigned)files_list.size());
    }
}

void IRAM_ATTR isrBtnInc()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ButtonEvent evt = BTN_EVT_INC;
    if (buttonQueue)
        xQueueSendFromISR(buttonQueue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

void IRAM_ATTR isrBtnDec()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ButtonEvent evt = BTN_EVT_DEC;
    if (buttonQueue)
        xQueueSendFromISR(buttonQueue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

void IRAM_ATTR isrBtnPlayPause()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ButtonEvent evt = BTN_EVT_PLAY_PAUSE;
    if (buttonQueue)
        xQueueSendFromISR(buttonQueue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

void IRAM_ATTR isrPCFInt()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ButtonEvent evt = BTN_EVT_PCF_INT;
    if (buttonQueue)
        xQueueSendFromISR(buttonQueue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

// Configure all PCF8575 pins with pull up/pull down
void pcfConfigureAllOutputs()
{
    for (uint8_t pin = 0; pin < 16; ++pin)
    {
        PCF.write(pin, PCF_PULL_STATE);
    }
}

void buttonTask(void *param)
{
    ButtonEvent evt;
    TickType_t lastChange = 0;
    const TickType_t debounceTicks = pdMS_TO_TICKS(120);
    for (;;)
    {
        if (xQueueReceive(buttonQueue, &evt, portMAX_DELAY) == pdTRUE)
        {
            TickType_t now = xTaskGetTickCount();
            if (evt != BTN_EVT_PCF_INT && (now - lastChange < debounceTicks))
                continue;
            lastChange = now;

            if (evt == BTN_EVT_INC && currentBalance < 16)
            {
                currentBalance++;
                audio.setBalance(currentBalance);
                Serial.printf("[BTN IO14] Balance: %d\n", currentBalance);
            }
            else if (evt == BTN_EVT_DEC && currentBalance > -16)
            {
                currentBalance--;
                audio.setBalance(currentBalance);
                Serial.printf("[BTN IO16] Balance: %d\n", currentBalance);
            }
            else if (evt == BTN_EVT_PLAY_PAUSE)
            {
                audio.pauseResume();
                Serial.printf("[BTN IO33] %s\n", audio.isRunning() ? "Resumed" : "Paused");
            }
            else if (evt == BTN_EVT_PCF_INT)
            {
                pcfConfigureAllOutputs();
                delay(1);
                uint16_t pcfValue = 0;
                for (uint8_t pin = 0; pin < 16; ++pin)
                {
                    if (PCF.read(pin)) pcfValue |= (1u << pin);
                }
                Serial.printf("[PCF8575 INT] value=0x%04X\n", pcfValue);

                // Detect falling edges (HIGH -> LOW), PCF INT is active low
                uint16_t falling = (~pcfValue) & pcfPrevValue;
                if (falling)
                {
                    // Play the first matching pin index (lowest index wins)
                    for (uint8_t pin = 0; pin < 16; ++pin)
                    {
                        if (falling & (1u << pin))
                        {
                            if (pcfPlayQueue)
                            {
                                uint8_t idx = pin;
                                xQueueSend(pcfPlayQueue, &idx, 0);
                            }
                            break;
                        }
                    }
                }
                pcfPrevValue = pcfValue;
            }
        }
    }
}

String getContentType(String filename)
{
    if (filename.endsWith(".htm") || filename.endsWith(".html"))
        return "text/html";
    else if (filename.endsWith(".css"))
        return "text/css";
    else if (filename.endsWith(".js"))
        return "application/javascript";
    else if (filename.endsWith(".png"))
        return "image/png";
    else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg"))
        return "image/jpeg";
    else if (filename.endsWith(".gif"))
        return "image/gif";
    else if (filename.endsWith(".ico"))
        return "image/x-icon";
    else if (filename.endsWith(".xml"))
        return "text/xml";
    else if (filename.endsWith(".pdf"))
        return "application/pdf";
    else if (filename.endsWith(".zip"))
        return "application/zip";
    else if (filename.endsWith(".gz"))
        return "application/x-gzip";

    return "text/plain";
}

String local_vars_to_json()
{
    StaticJsonDocument<2048> doc;

    doc["loop_file"] = loop_file;
    doc["auto_play"] = auto_play;
    doc["bt_mode"] = bt_mode;
    doc["note"] = note;
    doc["udp_port"] = localPort;
    doc["volume"] = volume;
    for (std::vector<t_music_data>::size_type i = 0; i != music_data.size(); i++)
    {
        doc["track_assignation"][i]["path"] = music_data[i].path;
        doc["track_assignation"][i]["index"] = music_data[i].index;
    }
    String buff;
    serializeJsonPretty(doc, buff);
    return (buff);
}

void update_spiffs()
{
    char *filePath = "/data.json";
    fs::File file = SPIFFS.open(filePath, "w");
    file.print(local_vars_to_json().c_str());
    file.close();
}

void json_to_local_vars(uint8_t *data)
{
    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, data);
    if (error)
    {
        Serial.println(error.c_str());
        return;
    }

    if (doc.containsKey("loop_file"))
    {
        bool tmp_loop_file = doc["loop_file"].as<const bool>();
        if (tmp_loop_file != loop_file)
        {
            loop_file = tmp_loop_file;
            audio.setFileLoop(loop_file);
        }
    }
    if (doc.containsKey("auto_play"))
        auto_play = doc["auto_play"].as<const bool>();
    if (doc.containsKey("bt_mode"))
        bt_mode = doc["bt_mode"].as<const bool>();
    if (doc.containsKey("note"))
        note = doc["note"].as<String>();
    if (doc.containsKey("udp_port"))
        localPort = doc["udp_port"].as<unsigned int>();
    if (doc.containsKey("volume"))
    {
        uint8_t tmp_volume = doc["volume"].as<unsigned char>();
        if (tmp_volume != volume)
        {
            volume = tmp_volume;
            audio.setVolume(volume);
        }
    }
    if (doc.containsKey("track_assignation"))
    {
        music_data.clear();
        for (uint16_t i = 0; i < doc["track_assignation"].size(); i++)
        {
            music_data.push_back((t_music_data){.path = doc["track_assignation"][i]["path"].as<String>(),
                                                .index = doc["track_assignation"][i]["index"].as<unsigned char>()});
        }
    }
}

void load_spiffs()
{
    char *filePath = "/data.json";
    fs::File file = SPIFFS.open(filePath, "r");
    if (!file)
    {
        Serial.println("Failed to open file for reading");
        return;
    }
    Serial.printf("File Size : %d\n", file.size());
    Serial.printf("Free Heap : %d\n", ESP.getFreeHeap());
    uint8_t *buff;
    size_t read_index = 1;
    size_t buff_size = file.size();
    buff = (uint8_t *)malloc(sizeof(char) * (buff_size + 1));
    if ((read_index = file.read(buff, (buff_size))) > 0)
    {
        buff[read_index] = '\0';
        Serial.printf("[SPIFFS]\n%s", buff);
    }
    else
    {
        Serial.printf("Error reading SPIFFS");
    }

    json_to_local_vars(buff);

    free(buff);
    file.close();

    Serial.printf("SPIFFS loop_file : %s\n", loop_file ? "true" : "false");
    Serial.printf("SPIFFS auto_play : %s\n", auto_play ? "true" : "false");
    Serial.printf("SPIFFS bt_mode : %s\n", bt_mode ? "true" : "false");
    Serial.printf("SPIFFS note : %s\n", note.c_str());
    Serial.printf("SPIFFS udp_port : %d\n", localPort);
    Serial.printf("SPIFFS volume : %d\n", volume);

    for (std::vector<t_music_data>::size_type i = 0; i != music_data.size(); i++)
    {
        Serial.printf("SPIFFS track_assignation (path:'%s' index:%d)\n", music_data[i].path.c_str(), music_data[i].index);
    }
}

void load_json_config_on_sd(const char *filename)
{
    File jsonFile = SD.open(filename, FILE_READ);
    if (!jsonFile)
    {
        Serial.println("Json Config file not found on SD card !");
        return;
    }

    String jsonContent;
    while (jsonFile.available())
    {
        jsonContent += char(jsonFile.read());
    }
    jsonFile.close();

    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, jsonContent);
    if (error)
    {
        Serial.println(error.c_str());
        return;
    }
    if (doc.containsKey("ssid"))
    {
        ssid = doc["ssid"].as<String>();
        Serial.print("ssid on sd card :");
        Serial.println(ssid);
    }
    if (doc.containsKey("password"))
    {
        password = doc["password"].as<String>();
        Serial.print("password on sd card :");
        Serial.println(password);
    }
}

std::vector<String> listSdFiles(const char *dirname)
{
    std::vector<String> fileList;

    // Ouvrir le répertoire spécifié
    File root = SD.open(dirname);

    // Vérifier si le répertoire a pu être ouvert
    if (!root)
    {
        Serial.println("Erreur lors de l'ouverture du répertoire !");
        return fileList;
    }

    // Parcourir tous les fichiers du répertoire
    while (true)
    {
        File entry = root.openNextFile();

        // Arrêter la boucle lorsque tous les fichiers ont été listés
        if (!entry)
        {
            break;
        }
        String nameString = String(entry.name());
        nameString.toLowerCase();
        if (!nameString.startsWith("/.") && (nameString.endsWith(".wav") || nameString.endsWith(".mp3")))
        {
            // Afficher le nom du fichier
            Serial.println(entry.name());
            fileList.push_back(String(entry.name()));
        }
        // Fermer le fichier
        entry.close();
    }

    // Fermer le répertoire
    root.close();

    // Trier la liste des fichiers dans l'ordre alphabétique
    std::sort(fileList.begin(), fileList.end());

    return fileList;
}

void update_music_from_sd()
{
    files_list = listSdFiles("/");
    music_data.clear();
    for (std::vector<String>::size_type i = 0; i != files_list.size(); i++)
    {
        music_data.push_back((t_music_data){.path = files_list[i],
                                            .index = i});
    }
}

void handleDelete(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    audio.stopSong();

    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, data);
    if (error)
    {
        Serial.println(error.c_str());
        return;
    }
    int file_index = doc["index"].as<unsigned int>();
    Serial.printf("Removing file %d\n", file_index);
    SD.remove(files_list[file_index].c_str());
    update_music_from_sd();
    request->send(200);
}

void handleStop(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    audio.stopSong();
    request->send(200);
}

void handleSettings(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    Serial.printf("Handle settings : %s\n", data);
    json_to_local_vars(data);
    update_spiffs();

    // StaticJsonDocument<2048> doc;
    printf("Json to send : %s\n", local_vars_to_json().c_str());
    // DeserializationError error = deserializeJson(doc, data);
    // if (error)
    // {
    //     Serial.println(error.c_str());
    //     return;
    // }
    // int file_index = doc["index"].as<unsigned int>();
    // Serial.printf("Playing file %d\n", file_index);
    // audio.connecttoFS(SD, files_list[file_index].c_str());

    request->send(200);
}

void handlePlay(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, data);
    if (error)
    {
        Serial.println(error.c_str());
        return;
    }
    int file_index = doc["index"].as<unsigned int>();
    Serial.printf("Playing file %d\n", file_index);
    audio.connecttoFS(SD, files_list[file_index].c_str());
    if (loop_file)
    {
        audio.setFileLoop(true);
    }
    request->send(200);
}
void handleRequest(AsyncWebServerRequest *request)
{
    String filePath = request->url(); // Obtenez l'URL demandée

    // Vérifier si le fichier existe sur la carte SD
    if (SD.exists(filePath))
    {
        // Ouvrir le fichier en lecture
        File file = SD.open(filePath);

        // Vérifier si le fichier a été ouvert avec succès
        if (file)
        {
            // Envoyer l'en-tête de réponse avec le type MIME approprié
            String contentType = getContentType(filePath);
            request->send(file, contentType);

            // Fermer le fichier
            file.close();
            return;
        }
    }

    // If not found on SD, serve portal index to keep captive experience
    request->send(SPIFFS, "/index.html", String(), false);
}
File uploadFile;
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (request->url() != "/edit")
        return;
    // S'il s'agit du premier fragment du fichier, ouvrir le fichier en écriture
    if (index == 0)
    {
        if (SD.exists((char *)filename.c_str()))
            SD.remove((char *)filename.c_str());
        uploadFile = SD.open(filename.c_str(), FILE_WRITE);
        Serial.print("Upload: START, filename: ");
        Serial.println(filename);
        // Serial.println(filename);
        Serial.printf("Len : %d\n", len);
    }

    // Écrire les données dans le fichier
    if (uploadFile)
    {
        if (uploadFile)
            uploadFile.write(data, len);
        Serial.print("Upload: WRITE, Bytes: ");
        Serial.println(len);
    }

    // S'il s'agit du dernier fragment du fichier, fermer le fichier
    if (final)
    {
        if (uploadFile)
            uploadFile.close();
        Serial.print("Upload: END :");
        Serial.println(len);
        update_music_from_sd();
        request->send(200);
    }
}

// Simple I2C scanner to help debug PCF presence on the bus
void scanI2CBus()
{
    byte error;
    byte address;
    int nDevices = 0;
    Serial.println("I2C scan start");
    for (address = 1; address < 127; address++)
    {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0)
        {
            Serial.printf("I2C device found at 0x%02X\n", address);
            nDevices++;
        }
        else if (error == 4)
        {
            Serial.printf("Unknown error on address 0x%02X\n", address);
        }
    }
    if (nDevices == 0)
    {
        Serial.println("No I2C devices found");
    }
    else
    {
        Serial.printf("I2C scan done, %d device(s) found\n", nDevices);
    }
}



void setup()
{
    pinMode(SD_CS, OUTPUT);
    pinMode(I2S_ENABLE, OUTPUT);
    digitalWrite(I2S_ENABLE, 1);
    // Configure BT control buttons
    pinMode((int)START_BUTTON, INPUT);
    esp_sleep_enable_ext0_wakeup(START_BUTTON, 0);
    // Configure RTC domain pull state so EXT0 sees a stable HIGH when released
    rtc_gpio_pulldown_dis((gpio_num_t)START_BUTTON);
    rtc_gpio_pullup_en((gpio_num_t)START_BUTTON);
    pinMode(DOWN_BUTTON, INPUT);
    pinMode(UP_BUTTON, INPUT);
    pinMode(CHRG_STATUS, INPUT);
    pinMode(LED_IO4, OUTPUT);
    pinMode(USB_VOLTAGE_PIN, INPUT);
    pinMode(BATTERY_VOLTAGE_PIN, INPUT);
    digitalWrite(LED_IO4, LOW);

    // Initialize NeoPixel strip
    strip.begin();
    strip.setBrightness(10);
    strip.show();

    // Create button event queue and task
    buttonQueue = xQueueCreate(8, sizeof(ButtonEvent));
    if (buttonQueue)
    {
        xTaskCreate(buttonTask, "buttonTask", 4096, NULL, 1, &buttonTaskHandle);
    }

    // Configure local balance buttons on IO14 / IO16 (active LOW)
    pinMode(BTN_INC_BAL, INPUT_PULLUP);
    pinMode(BTN_DEC_BAL, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BTN_INC_BAL), BtnInc, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BTN_DEC_BAL), BtnDec, CHANGE);

  // Configure ADC width and per-pin attenuation (11 dB ~ up to ~3.3 V)
  analogSetWidth(12);
  analogSetPinAttenuation(USB_VOLTAGE_PIN, ADC_11db);
  analogSetPinAttenuation(BATTERY_VOLTAGE_PIN, ADC_11db);
    // Attach BT control interrupts
    attachInterrupt(digitalPinToInterrupt((int)START_BUTTON), StartPress, CHANGE);
    attachInterrupt(digitalPinToInterrupt(DOWN_BUTTON), DownPress, CHANGE);
    attachInterrupt(digitalPinToInterrupt(UP_BUTTON), UpPress, CHANGE);
    // PCF8575 interrupt line on GPIO15
    pinMode(PCF_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PCF_INT_PIN), isrPCFInt, FALLING);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    Serial.begin(115200);

    // Initialize I2C explicitly on ESP32 default pins (SDA=21, SCL=22)
    Wire.begin(21, 22);
    Wire.setClock(100000); // 100 kHz for robust scanning

    if (!PCF.begin())
    {
      Serial.println("could not initialize...");
    }
    if (!PCF.isConnected())
    {
      Serial.println("=> not connected");
      scanI2CBus();
    }
    else
    {
      Serial.println("=> connected!!");
      // Configure all lines as inputs and read their initial states
      pcfConfigureAllOutputs();
      // Prime previous value with current readout to avoid false edges
      uint16_t initValue = 0;
      for (uint8_t pin = 0; pin < 16; ++pin)
      {
          if (PCF.read(pin)) initValue |= (1u << pin);
      }
      pcfPrevValue = initValue;
    }

    // Skip PCF queue in BT-only mode

    if (!SD.begin(SD_CS))
    {
        Serial.println("SD initialization failed!");
        while (1)
            ;
    }
    Serial.println("SD initialization done.");
    if (!SPIFFS.begin())
    {
        Serial.println("SPIFFS initialization failed !");
        while (1)
            ;
        // return;
    }
    Serial.println("SPIFFS initialization done.");
    load_json_config_on_sd("/config.json");

    load_spiffs();
    Serial.printf("JSON : %s/n", local_vars_to_json().c_str());
    if (!bt_mode)
    {
        WiFi.mode(WIFI_AP);
        // Optional: ensure a common AP IP like 192.168.4.1
        WiFi.softAP(ssid.c_str(), password.c_str());

        // Start DNS server for captive portal: resolve all domains to AP IP
        dnsServer.start(53, "*", WiFi.softAPIP());

        Serial.print("Starting AP \"" + ssid + "\"");
        delay(100);
        Serial.println("... done");
        digitalWrite(2, 0); ///
        udp.begin(localPort);
        if (DEBUG)
        {
            Serial.begin(115200);
            Serial.print("IP: ");
            Serial.println(WiFi.softAPIP());
        }
        server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
                  { request->send(SPIFFS, "/index.html", String(), false); });
        // Captive portal endpoints used by OS connectivity checks
        // Android
        server.on("/generate_204", HTTP_ANY, [](AsyncWebServerRequest *request) { request->send(200, "text/html", "<html><meta http-equiv=\"refresh\" content=\"0; url=/\"></html>"); });
        // iOS/macOS
        server.on("/hotspot-detect.html", HTTP_ANY, [](AsyncWebServerRequest *request) { request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"); });
        server.on("/success.html", HTTP_ANY, [](AsyncWebServerRequest *request) { request->send(200, "text/html", "Success"); });
        // Windows
        server.on("/ncsi.txt", HTTP_ANY, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", "Microsoft NCSI"); });
        server.on("/connecttest.txt", HTTP_ANY, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", "" ); });
        server.on("/library/test/success.html", HTTP_ANY, [](AsyncWebServerRequest *request) { request->send(200, "text/html", "Success"); });
        server.on("/success.txt", HTTP_ANY, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", "Success"); });
        server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request)
                  {Serial.printf("Json demandé par le site\n");
                  request->send(200, "application/json", local_vars_to_json()); });
        server.on(
            "/play", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handlePlay);
        server.on(
            "/stop", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleStop);
        server.on(
            "/delete", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleDelete);
        server.on(
            "/settings", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleSettings);
        server.onFileUpload(handleFileUpload);
        // server.on("/edit", HTTP_POST, handleFileUpload2);

        server.serveStatic("/index.css", SPIFFS, "/index.css");
        server.serveStatic("/index.js", SPIFFS, "/index.js");
        // server.serveStatic("/background.png", SPIFFS, "/background.png");
        // server.serveStatic("/react.svg", SPIFFS, "/react.svg");
        server.serveStatic("/vite.svg", SPIFFS, "/vite.svg");
        server.onNotFound(handleRequest);

        server.begin();
    }

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    update_music_from_sd();
    // printf("test : %s\n", files_list[1].c_str());
    if (auto_play && !bt_mode)
    {
        if (!files_list.empty())
        {
            audio.connecttoFS(SD, files_list[0].c_str());
        }
    }
    if (loop_file)
    {
        audio.setFileLoop(true);
    }

    audio.setVolumeSteps(255); // max 255
    audio.setVolume(volume);
    // Initialize balance to center on boot
    currentBalance = 0;
    audio.setBalance(currentBalance);
    // Start BT audio at the very end if requested
    if (bt_mode)
    {
        startBtMode();
    }
}

bool need_to_play = true;

uint16_t current_Volume = 4095;
char packetBuffer[255]; // Incoming

// Méthode pour découper le message avec un séparateur (ou "parser")
void splitString(String message, char separator, String data[5])
{

    int index = 0;
    int cnt = 0;
    do
    {
        index = message.indexOf(separator);
        // s'il y a bien un caractère séparateur
        if (index != -1)
        {
            // on découpe la chaine et on stocke le bout dans le tableau
            data[cnt] = message.substring(0, index);
            cnt++;
            // on enlève du message le bout stocké
            message = message.substring(index + 1, message.length());
        }
        else
        {
            // après le dernier espace
            // on s'assure que la chaine n'est pas vide
            if (message.length() > 0)
            {
                data[cnt] = message.substring(0, index); // dernier bout
                cnt++;
            }
        }
    } while (index >= 0); // tant qu'il y a bien un séparateur dans la chaine
}

void loop()
{
    audio.loop();
    // Prefer calibrated millivolts API for better accuracy on ESP32
    uint32_t usbMv = analogReadMilliVolts(USB_VOLTAGE_PIN);
    usbVoltage = (usbMv / 1000.0f) * USB_DIVIDER_GAIN;
    uint32_t batMv = analogReadMilliVolts(BATTERY_VOLTAGE_PIN);
    // Note: ADC2 (GPIO13) reads 0 when WiFi is active; move battery sense to an ADC1 pin
    static bool warnedAdc2 = false;
    if (batMv == 0 && !warnedAdc2)
    {
        Serial.println("[Battery] ADC2 pin may be blocked by WiFi. Use an ADC1 pin (e.g., 32-35/36-39).");
        warnedAdc2 = true;
    }
    if (batMv > 0)
    {
        batteryVoltage = (batMv / 1000.0f) * BATTERY_DIVIDER_GAIN;
    }
    // Blink LEDs on IO12 and IO4 (toggle every 500 ms)
    static uint32_t lastBlinkMs = 0;
    static bool blinkState = false;
    uint32_t nowMs = millis();
    if (nowMs - lastBlinkMs >= 1000)
    {
        Serial.printf("USB Voltage: %f V, Battery Voltage: %f V\n", usbVoltage, batteryVoltage);
        lastBlinkMs = nowMs;
        blinkState = !blinkState;
        uint8_t chrgStatus = digitalRead(CHRG_STATUS);
        // If charge status is 1, then the battery is charging
        Serial.printf("CHRG Status: %d\n", chrgStatus);
        // digitalWrite(CHRG_STATUS, blinkState ? LOW : HIGH);
    }
    // LED indication:
    // - Pairing (BT started but not connected): rainbow
    // - Otherwise: battery indicator
    if (bt_mode && !a2dpConnected)
    {
        updateRainbow(nowMs);
    }
    else
    {
        bool isCharging = (usbVoltage > 4.0f) && (digitalRead(CHRG_STATUS) == 1);
        updateBatteryLeds(nowMs, usbVoltage, batteryVoltage, isCharging);
    }
    // PCF8575 interrupt processing moved to FreeRTOS task (pcfIntTask)
    // Button handling moved to ISR + FreeRTOS task
    // Handle deferred playback requests from PCF task
    uint8_t playIdx;
    while (pcfPlayQueue && xQueueReceive(pcfPlayQueue, &playIdx, 0) == pdTRUE)
    {
        playTrackIndex(playIdx);
    }
    // UDP/WiFi stack removed in BT-only firmware
}

// optional
void audio_info(const char *info)
{
    Serial.print("info        ");
    Serial.println(info);
}
void audio_id3data(const char *info)
{ // id3 metadata
    Serial.print("id3data     ");
    Serial.println(info);
}
void audio_eof_mp3(const char *info)
{ // end of file
    Serial.print("eof_mp3     ");
    Serial.println(info);
    need_to_play = true;
    //  audio.connecttoFS(SD, "/audio.wav");     // SD
}
void audio_showstation(const char *info)
{
    Serial.print("station     ");
    Serial.println(info);
}
void audio_showstreamtitle(const char *info)
{
    Serial.print("streamtitle ");
    Serial.println(info);
}
void audio_bitrate(const char *info)
{
    Serial.print("bitrate     ");
    Serial.println(info);
}
void audio_commercial(const char *info)
{ // duration in sec
    Serial.print("commercial  ");
    Serial.println(info);
}
void audio_icyurl(const char *info)
{ // homepage
    Serial.print("icyurl      ");
    Serial.println(info);
}
void audio_lasthost(const char *info)
{ // stream URL played
    Serial.print("lasthost    ");
    Serial.println(info);
}
void audio_eof_speech(const char *info)
{
    Serial.print("eof_speech  ");
    Serial.println(info);
}
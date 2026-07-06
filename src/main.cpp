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
//
// Stream : "S http://host:port/path" pour lancer un flux HTTP (MP3/AAC)
// Stop : "X" pour arreter la lecture en cours
// Discovery : "?" pour recevoir un JSON de decouverte en UDP

#include "Arduino.h"
#include "WiFi.h"
#include <ESPmDNS.h>
#include "jsonParser.h"
#include "ESPAsyncWebServer.h"
#include "Audio.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "SD.h"
#include "FS.h"
#include "sstream"
#include <algorithm>
#include <Adafruit_NeoPixel.h>
#include "PCF8575.h"
#include "esp32-hal-adc.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "Wire.h"

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
#define START_BUTTON GPIO_NUM_33
#define CHRG_STATUS 13
#define USB_VOLTAGE_PIN 32
#define BATTERY_VOLTAGE_PIN 34
#define LED_IO4 4
#define PCF_INT_PIN 15
#define PCF_PULL_STATE LOW

// IO expander on I2C
PCF8575 PCF(0x20);
static uint16_t pcfPrevValue = 0xFFFF;
static volatile bool pcfInterruptPending = false;
static bool pcfReady = false;

unsigned int localPort = 8266;     // port de reception UDP

bool loop_file = true;               // Default loop audio files
bool auto_play = false;              // Lit la premiere track au demarrage
bool allow_play_over_playing = false; // Autoriser lancer une piste alors qu'une est deja en lecture
const bool DEBUG = true;             // Afficher les messages dans la console

// Physical configurable buttons are on IO14 and IO16.
// We keep legacy variable names for backward compatibility in saved config/web UI.
const uint8_t BUTTON_GPIO_13 = 14;
const uint8_t BUTTON_GPIO_16 = 16;
const uint16_t BUTTON_DEBOUNCE_MS = 40;
const uint8_t BUTTON_PULL_MODE_UP = 0;
const uint8_t BUTTON_PULL_MODE_DOWN = 1;
const uint8_t BUTTON_PULL_MODE_NONE = 2;
const uint8_t BUTTON_ACTIVE_LEVEL_LOW = 0;
const uint8_t BUTTON_ACTIVE_LEVEL_HIGH = 1;
const uint8_t PCF_PULL_MODE_UP = 0;
const uint8_t PCF_PULL_MODE_DOWN = 1;
const uint8_t PCF_ACTIVE_LEVEL_LOW = 0;
const uint8_t PCF_ACTIVE_LEVEL_HIGH = 1;
const uint8_t PCF_CHANNEL_COUNT = 16;
const uint16_t REMOTE_PLAYBACK_DEDUP_WINDOW_MS = 250;
const uint8_t ESP_NOW_BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t ESP_NOW_PACKET_MAGIC = 0xA5;
const uint8_t ESP_NOW_PACKET_VERSION_LEGACY = 0x01;
const uint8_t ESP_NOW_PACKET_VERSION = 0x02;
const uint8_t ESP_NOW_CMD_PLAY_TRACK = 0x01;
const uint8_t DEVICE_MODE_CURRENT = 0;
const uint8_t DEVICE_MODE_MESH = 1;
const uint8_t DEVICE_MODE_AP_OFF = 2;
const uint8_t DEVICE_MODE_RELAY_ONLY = 3;
const uint8_t DEVICE_MODE_AP_STA_CLIENT = 4;
const uint8_t MESH_DEFAULT_TTL = 3;
const uint8_t MESH_MAX_TTL = 8;
const uint8_t MESH_SEEN_CACHE_SIZE = 32;
const uint16_t AP_SAFETY_TIMEOUT_DEFAULT_S = 300;
const uint16_t AP_SAFETY_TIMEOUT_MAX_S = 3600;
const size_t SETTINGS_DOC_CAPACITY = 6144;
const char *WEB_SERIAL_PROVISION_STATE_FILE = "/.webcfg.done";
const char *WEB_SERIAL_PROVISION_PREFIX = "WEBCFG:";
const uint32_t WEB_SERIAL_PROVISION_WINDOW_MS = 10000;
const uint32_t WEB_SERIAL_PROVISION_READY_INTERVAL_MS = 300;
const size_t WEB_SERIAL_PROVISION_MAX_LINE = 4096;

String ap_name = "I2S-SD-DEFAULT";
String ap_ssid = "";
String ap_password = "12345678";
String ap_ip = "";
String ap_ip_config = "192.168.4.1";
String sta_ssid = "";
String sta_password = "";
String sta_ip = "";
String sta_ip_config = "";
String sta_gateway_config = "";
String sta_subnet_config = "255.255.255.0";
bool sta_use_dhcp = true;
bool sta_connected = false;
uint8_t esp_now_channel = 6;
uint8_t device_mode = DEVICE_MODE_CURRENT;
uint8_t mesh_ttl = MESH_DEFAULT_TTL;
uint16_t ap_safety_timeout_s = AP_SAFETY_TIMEOUT_DEFAULT_S;
int16_t button_gpio13_track = 0;
int16_t button_gpio16_track = 1;
uint8_t button_gpio13_pull_mode = BUTTON_PULL_MODE_UP;
uint8_t button_gpio16_pull_mode = BUTTON_PULL_MODE_UP;
uint8_t button_gpio13_active_level = BUTTON_ACTIVE_LEVEL_LOW;
uint8_t button_gpio16_active_level = BUTTON_ACTIVE_LEVEL_LOW;
uint8_t pcf_pull_mode = PCF_PULL_MODE_UP;
uint8_t pcf_active_level = PCF_ACTIVE_LEVEL_LOW;
int16_t pcf_track_map[PCF_CHANNEL_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
bool esp_now_ready = false;
bool ap_runtime_enabled = false;
bool ap_safety_ap_activated = false;
uint32_t ap_safety_activated_at_ms = 0; // when safety AP was turned on (boot window or inactivity fallback)
volatile bool restart_requested = false;
uint32_t restart_requested_at_ms = 0;
uint32_t device_id = 0;
uint32_t esp_now_message_counter = 1;
uint32_t last_esp_now_activity_ms = 0;
float usbVoltage = 0.0f;
float batteryVoltage = 0.0f;
const float USB_DIVIDER_GAIN = 2.0f;
const float BATTERY_DIVIDER_GAIN = 2.0f;
uint8_t led_mode = 0; // 0 battery, 1 rainbow, 2 off, 3 solid
uint8_t led_brightness = 10;
uint8_t led_solid_r = 0;
uint8_t led_solid_g = 0;
uint8_t led_solid_b = 64;
static bool startButtonPressed = false;
static uint32_t startButtonPressedAtMs = 0;
static bool sleepRequested = false;

typedef struct __attribute__((packed)) s_esp_now_packet
{
    uint8_t magic;
    uint8_t version;
    uint8_t cmd;
    uint8_t ttl;
    uint16_t track_index;
    uint32_t origin_id;
    uint32_t message_id;
} t_esp_now_packet;

typedef struct __attribute__((packed)) s_esp_now_packet_legacy
{
    uint8_t magic;
    uint8_t version;
    uint8_t cmd;
    uint8_t reserved;
    uint16_t track_index;
} t_esp_now_packet_legacy;

typedef struct s_mesh_seen_message
{
    uint32_t origin_id;
    uint32_t message_id;
} t_mesh_seen_message;

typedef struct s_button_state
{
    uint8_t gpio;
    bool stable_state;
    bool last_reading;
    uint32_t last_change_ms;
} t_button_state;

t_button_state button_states[2] = {
    {.gpio = BUTTON_GPIO_13, .stable_state = true, .last_reading = true, .last_change_ms = 0},
    {.gpio = BUTTON_GPIO_16, .stable_state = true, .last_reading = true, .last_change_ms = 0}};

volatile bool esp_now_pending_packet = false;
t_esp_now_packet esp_now_packet_to_handle = {};
volatile uint8_t esp_now_packet_format_to_handle = 0;
t_mesh_seen_message mesh_seen_messages[MESH_SEEN_CACHE_SIZE] = {};
uint8_t mesh_seen_cursor = 0;
uint16_t last_remote_played_track = 0xFFFF;
uint32_t last_remote_played_at_ms = 0;
portMUX_TYPE esp_now_mux = portMUX_INITIALIZER_UNLOCKED;
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
uint8_t volume = 60;
std::vector<t_music_data> music_data;
std::vector<String> files_list;
enum AudioSource : uint8_t
{
    AUDIO_SOURCE_NONE = 0,
    AUDIO_SOURCE_SD = 1,
    AUDIO_SOURCE_STREAM = 2,
};

Audio audio;
WiFiUDP udp;
AsyncWebServer server(80);
AudioSource audioSource = AUDIO_SOURCE_NONE;
String stream_url = "";
static const uint8_t NEO_PIN = LED_IO4;
static const uint16_t NEO_COUNT = 4;
static Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
static uint16_t rainbowOffset = 0;
static uint32_t lastRainbowMs = 0;

bool is_ap_enabled_mode(uint8_t mode);
bool is_relay_mode(uint8_t mode);

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
    else if (filename.endsWith(".wav"))
        return "audio/wav";
    else if (filename.endsWith(".mp3"))
        return "audio/mpeg";
    else if (filename.endsWith(".m4a"))
        return "audio/mp4";
    else if (filename.endsWith(".aac"))
        return "audio/aac";
    else if (filename.endsWith(".flac"))
        return "audio/flac";
    else if (filename.endsWith(".ogg"))
        return "audio/ogg";

    return "text/plain";
}

int8_t hex_char_to_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

String decode_url_path(const String &encoded_path)
{
    String decoded_path = "";
    decoded_path.reserve(encoded_path.length());

    for (size_t i = 0; i < encoded_path.length(); i++)
    {
        char current_char = encoded_path.charAt(i);
        if (current_char == '%' && i + 2 < encoded_path.length())
        {
            int8_t high = hex_char_to_value(encoded_path.charAt(i + 1));
            int8_t low = hex_char_to_value(encoded_path.charAt(i + 2));
            if (high >= 0 && low >= 0)
            {
                decoded_path += (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        decoded_path += current_char;
    }
    return decoded_path;
}

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

void IRAM_ATTR onPcfInterrupt()
{
    pcfInterruptPending = true;
}

void apply_pcf_input_config()
{
    if (!pcfReady)
    {
        return;
    }
    bool pullHigh = (pcf_pull_mode == PCF_PULL_MODE_UP);
    for (uint8_t pin = 0; pin < PCF_CHANNEL_COUNT; ++pin)
    {
        PCF.write(pin, pullHigh ? HIGH : LOW);
    }

    uint16_t initValue = 0;
    for (uint8_t pin = 0; pin < PCF_CHANNEL_COUNT; ++pin)
    {
        if (PCF.read(pin))
        {
            initValue |= (1u << pin);
        }
    }
    pcfPrevValue = initValue;
}

void request_sleep()
{
    sleepRequested = true;
}

void update_hardware_power_button()
{
    bool pressed = (digitalRead((int)START_BUTTON) == LOW);
    uint32_t now = millis();
    if (pressed && !startButtonPressed)
    {
        startButtonPressed = true;
        startButtonPressedAtMs = now;
    }
    else if (!pressed && startButtonPressed)
    {
        startButtonPressed = false;
    }
    if (startButtonPressed && (uint32_t)(now - startButtonPressedAtMs) >= 2000U)
    {
        request_sleep();
        startButtonPressed = false;
    }
}

void apply_sleep_if_requested()
{
    if (!sleepRequested)
    {
        return;
    }
    sleepRequested = false;
    Serial.println("Entering deep sleep...");
    digitalWrite(I2S_ENABLE, LOW);
    strip.clear();
    strip.show();
    while (digitalRead((int)START_BUTTON) == LOW)
    {
        delay(10);
    }
    delay(50);
    esp_deep_sleep_start();
}

void update_power_measurements()
{
    uint32_t usbMv = analogReadMilliVolts(USB_VOLTAGE_PIN);
    usbVoltage = (usbMv / 1000.0f) * USB_DIVIDER_GAIN;
    uint32_t batMv = analogReadMilliVolts(BATTERY_VOLTAGE_PIN);
    if (batMv > 0)
    {
        batteryVoltage = (batMv / 1000.0f) * BATTERY_DIVIDER_GAIN;
    }
}

void update_leds(uint32_t nowMs)
{
    strip.setBrightness(led_brightness);
    if (led_mode == 2)
    {
        strip.clear();
        strip.show();
        return;
    }
    if (led_mode == 3)
    {
        uint32_t color = strip.Color(led_solid_r, led_solid_g, led_solid_b);
        for (uint16_t i = 0; i < NEO_COUNT; ++i)
        {
            strip.setPixelColor(i, color);
        }
        strip.show();
        return;
    }
    if (led_mode == 1)
    {
        if (nowMs - lastRainbowMs < 10)
        {
            return;
        }
        lastRainbowMs = nowMs;
        for (uint16_t i = 0; i < NEO_COUNT; ++i)
        {
            uint8_t wheelPos = (uint8_t)(((i * 64) / NEO_COUNT + rainbowOffset) & 0xFF);
            strip.setPixelColor(i, colorWheel(wheelPos));
        }
        strip.show();
        rainbowOffset++;
        return;
    }

    bool isCharging = (usbVoltage > 4.0f) && (digitalRead(CHRG_STATUS) == HIGH);
    float v = batteryVoltage;
    const float vMin = 3.3f;
    const float vMax = 4.2f;
    if (v < vMin)
        v = vMin;
    if (v > vMax)
        v = vMax;
    float pct = (v - vMin) / (vMax - vMin);
    int greenCount = (int)(pct * NEO_COUNT + 0.5f);
    if (greenCount < 0)
        greenCount = 0;
    if (greenCount > (int)NEO_COUNT)
        greenCount = NEO_COUNT;

    static bool blinkOn = false;
    static uint32_t lastBlinkMs = 0;
    if (isCharging && nowMs - lastBlinkMs >= 500)
    {
        lastBlinkMs = nowMs;
        blinkOn = !blinkOn;
    }
    if (!isCharging)
    {
        blinkOn = true;
    }

    const uint32_t green = strip.Color(0, 150, 0);
    const uint32_t red = strip.Color(180, 0, 0);
    const uint32_t off = strip.Color(0, 0, 0);
    for (uint16_t i = 0; i < NEO_COUNT; ++i)
    {
        if ((int)i < greenCount)
        {
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

String local_vars_to_json()
{
    DynamicJsonDocument doc(SETTINGS_DOC_CAPACITY);

    doc["loop_file"] = loop_file;
    doc["auto_play"] = auto_play;
    doc["allow_play_over_playing"] = allow_play_over_playing;
    doc["note"] = note;
    doc["udp_port"] = localPort;
    doc["volume"] = volume;
    doc["ap_ssid"] = ap_ssid;
    doc["ap_ip"] = ap_ip;
    doc["ap_name"] = ap_name;
    doc["ap_password"] = ap_password;
    doc["ap_ip_config"] = ap_ip_config;
    doc["sta_ssid"] = sta_ssid;
    doc["sta_password"] = sta_password;
    doc["sta_use_dhcp"] = sta_use_dhcp;
    doc["sta_ip_config"] = sta_ip_config;
    doc["sta_gateway_config"] = sta_gateway_config;
    doc["sta_subnet_config"] = sta_subnet_config;
    doc["sta_ip"] = sta_ip;
    doc["sta_connected"] = sta_connected;
    doc["esp_now_channel"] = esp_now_channel;
    doc["device_mode"] = device_mode;
    doc["mesh_ttl"] = mesh_ttl;
    doc["ap_safety_timeout_s"] = ap_safety_timeout_s;
    doc["ap_runtime_enabled"] = ap_runtime_enabled;
    doc["button_gpio13_track"] = button_gpio13_track;
    doc["button_gpio14_track"] = button_gpio13_track;
    doc["button_gpio16_track"] = button_gpio16_track;
    doc["button_gpio13_pull_mode"] = button_gpio13_pull_mode;
    doc["button_gpio14_pull_mode"] = button_gpio13_pull_mode;
    doc["button_gpio16_pull_mode"] = button_gpio16_pull_mode;
    doc["button_gpio13_active_level"] = button_gpio13_active_level;
    doc["button_gpio14_active_level"] = button_gpio13_active_level;
    doc["button_gpio16_active_level"] = button_gpio16_active_level;
    doc["pcf_pull_mode"] = pcf_pull_mode;
    doc["pcf_active_level"] = pcf_active_level;
    for (uint8_t i = 0; i < PCF_CHANNEL_COUNT; i++)
    {
        doc["pcf_track_map"][i] = pcf_track_map[i];
    }
    doc["usb_voltage"] = usbVoltage;
    doc["battery_voltage"] = batteryVoltage;
    doc["charge_status"] = digitalRead(CHRG_STATUS) == HIGH;
    doc["led_mode"] = led_mode;
    doc["led_brightness"] = led_brightness;
    doc["led_solid_r"] = led_solid_r;
    doc["led_solid_g"] = led_solid_g;
    doc["led_solid_b"] = led_solid_b;
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
    const char *filePath = "/data.json";
    fs::File file = SPIFFS.open(filePath, "w");
    file.print(local_vars_to_json().c_str());
    file.close();
}

void json_to_local_vars(const uint8_t *data, size_t data_len)
{
    DynamicJsonDocument doc(SETTINGS_DOC_CAPACITY);

    DeserializationError error = deserializeJson(doc, data, data_len);
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
    if (doc.containsKey("allow_play_over_playing"))
        allow_play_over_playing = doc["allow_play_over_playing"].as<const bool>();
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
    if (doc.containsKey("button_gpio13_track"))
    {
        int16_t tmp_track = doc["button_gpio13_track"].as<int>();
        button_gpio13_track = tmp_track < -1 ? -1 : tmp_track;
    }
    else if (doc.containsKey("button_gpio14_track"))
    {
        int16_t tmp_track = doc["button_gpio14_track"].as<int>();
        button_gpio13_track = tmp_track < -1 ? -1 : tmp_track;
    }
    if (doc.containsKey("button_gpio16_track"))
    {
        int16_t tmp_track = doc["button_gpio16_track"].as<int>();
        button_gpio16_track = tmp_track < -1 ? -1 : tmp_track;
    }
    if (doc.containsKey("button_gpio13_pull_mode"))
    {
        button_gpio13_pull_mode = doc["button_gpio13_pull_mode"].as<unsigned int>();
    }
    else if (doc.containsKey("button_gpio14_pull_mode"))
    {
        button_gpio13_pull_mode = doc["button_gpio14_pull_mode"].as<unsigned int>();
    }
    if (doc.containsKey("button_gpio16_pull_mode"))
    {
        button_gpio16_pull_mode = doc["button_gpio16_pull_mode"].as<unsigned int>();
    }
    if (doc.containsKey("button_gpio13_active_level"))
    {
        button_gpio13_active_level = doc["button_gpio13_active_level"].as<unsigned int>();
    }
    else if (doc.containsKey("button_gpio14_active_level"))
    {
        button_gpio13_active_level = doc["button_gpio14_active_level"].as<unsigned int>();
    }
    if (doc.containsKey("button_gpio16_active_level"))
    {
        button_gpio16_active_level = doc["button_gpio16_active_level"].as<unsigned int>();
    }
    if (doc.containsKey("pcf_pull_mode"))
    {
        pcf_pull_mode = doc["pcf_pull_mode"].as<unsigned int>();
    }
    if (doc.containsKey("pcf_active_level"))
    {
        pcf_active_level = doc["pcf_active_level"].as<unsigned int>();
    }
    if (doc.containsKey("pcf_track_map"))
    {
        JsonArray pcfMap = doc["pcf_track_map"].as<JsonArray>();
        for (uint8_t i = 0; i < PCF_CHANNEL_COUNT; i++)
        {
            if (i < pcfMap.size())
            {
                pcf_track_map[i] = pcfMap[i].as<int>();
            }
        }
    }
    if (doc.containsKey("led_mode"))
    {
        led_mode = doc["led_mode"].as<unsigned int>();
    }
    if (doc.containsKey("led_brightness"))
    {
        led_brightness = doc["led_brightness"].as<unsigned int>();
    }
    if (doc.containsKey("led_solid_r"))
    {
        led_solid_r = doc["led_solid_r"].as<unsigned int>();
    }
    if (doc.containsKey("led_solid_g"))
    {
        led_solid_g = doc["led_solid_g"].as<unsigned int>();
    }
    if (doc.containsKey("led_solid_b"))
    {
        led_solid_b = doc["led_solid_b"].as<unsigned int>();
    }
    if (doc.containsKey("ap_ssid"))
        ap_ssid = doc["ap_ssid"].as<String>();
    else if (doc.containsKey("ssid"))
        ap_ssid = doc["ssid"].as<String>();
    if (doc.containsKey("ap_name"))
        ap_name = doc["ap_name"].as<String>();
    if (doc.containsKey("ap_password"))
        ap_password = doc["ap_password"].as<String>();
    if (doc.containsKey("ap_ip_config"))
        ap_ip_config = doc["ap_ip_config"].as<String>();
    else if (doc.containsKey("ap_ip"))
        ap_ip_config = doc["ap_ip"].as<String>();
    if (doc.containsKey("esp_now_channel"))
    {
        uint8_t tmp_channel = doc["esp_now_channel"].as<unsigned int>();
        if (tmp_channel >= 1 && tmp_channel <= 13)
            esp_now_channel = tmp_channel;
    }
    if (doc.containsKey("device_mode"))
    {
        uint8_t tmp_mode = doc["device_mode"].as<unsigned int>();
        if (tmp_mode <= DEVICE_MODE_AP_STA_CLIENT)
            device_mode = tmp_mode;
    }
    if (doc.containsKey("sta_ssid"))
        sta_ssid = doc["sta_ssid"].as<String>();
    if (doc.containsKey("sta_password"))
        sta_password = doc["sta_password"].as<String>();
    if (doc.containsKey("sta_use_dhcp"))
        sta_use_dhcp = doc["sta_use_dhcp"].as<const bool>();
    if (doc.containsKey("sta_ip_config"))
        sta_ip_config = doc["sta_ip_config"].as<String>();
    if (doc.containsKey("sta_gateway_config"))
        sta_gateway_config = doc["sta_gateway_config"].as<String>();
    if (doc.containsKey("sta_subnet_config"))
        sta_subnet_config = doc["sta_subnet_config"].as<String>();
    if (doc.containsKey("mesh_ttl"))
    {
        uint8_t tmp_ttl = doc["mesh_ttl"].as<unsigned int>();
        if (tmp_ttl >= 1 && tmp_ttl <= MESH_MAX_TTL)
            mesh_ttl = tmp_ttl;
    }
    if (doc.containsKey("ap_safety_timeout_s"))
    {
        int32_t tmp_timeout = doc["ap_safety_timeout_s"].as<int>();
        if (tmp_timeout >= 0 && tmp_timeout <= AP_SAFETY_TIMEOUT_MAX_S)
            ap_safety_timeout_s = (uint16_t)tmp_timeout;
    }
    if (doc.containsKey("track_assignation"))
    {
        music_data.clear();
        for (uint16_t i = 0; i < doc["track_assignation"].size(); i++)
        {
            music_data.push_back((t_music_data){.path = doc["track_assignation"][i]["path"].as<String>(),
                                                .index = (uint8_t)i});
        }
    }
}

void load_spiffs()
{
    const char *filePath = "/data.json";
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

    json_to_local_vars(buff, read_index);

    free(buff);
    file.close();

    Serial.printf("SPIFFS loop_file : %s\n", loop_file ? "true" : "false");
    Serial.printf("SPIFFS auto_play : %s\n", auto_play ? "true" : "false");
    Serial.printf("SPIFFS allow_play_over_playing : %s\n", allow_play_over_playing ? "true" : "false");
    Serial.printf("SPIFFS note : %s\n", note.c_str());
    Serial.printf("SPIFFS udp_port : %d\n", localPort);
    Serial.printf("SPIFFS volume : %d\n", volume);
    Serial.printf("SPIFFS ap_ssid : %s\n", ap_ssid.c_str());
    Serial.printf("SPIFFS ap_name : %s\n", ap_name.c_str());
    Serial.printf("SPIFFS ap_ip_config : %s\n", ap_ip_config.c_str());
    Serial.printf("SPIFFS esp_now_channel : %u\n", esp_now_channel);
    Serial.printf("SPIFFS device_mode : %u\n", device_mode);
    Serial.printf("SPIFFS sta_ssid : %s\n", sta_ssid.c_str());
    Serial.printf("SPIFFS sta_use_dhcp : %s\n", sta_use_dhcp ? "true" : "false");
    Serial.printf("SPIFFS mesh_ttl : %u\n", mesh_ttl);
    Serial.printf("SPIFFS ap_safety_timeout_s : %u\n", ap_safety_timeout_s);
    Serial.printf("SPIFFS button_gpio13_track : %d\n", button_gpio13_track);
    Serial.printf("SPIFFS button_gpio16_track : %d\n", button_gpio16_track);
    Serial.printf("SPIFFS button_gpio13_pull_mode : %u\n", button_gpio13_pull_mode);
    Serial.printf("SPIFFS button_gpio16_pull_mode : %u\n", button_gpio16_pull_mode);
    Serial.printf("SPIFFS button_gpio13_active_level : %u\n", button_gpio13_active_level);
    Serial.printf("SPIFFS button_gpio16_active_level : %u\n", button_gpio16_active_level);
    Serial.printf("SPIFFS pcf_pull_mode : %u\n", pcf_pull_mode);
    Serial.printf("SPIFFS pcf_active_level : %u\n", pcf_active_level);

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
    if (doc.containsKey("ap_ssid"))
    {
        ap_ssid = doc["ap_ssid"].as<String>();
        Serial.print("ap_ssid on sd card :");
        Serial.println(ap_ssid);
    }
    else if (doc.containsKey("ssid"))
    {
        ap_ssid = doc["ssid"].as<String>();
        Serial.print("legacy ssid reused as ap_ssid :");
        Serial.println(ap_ssid);
    }
    if (doc.containsKey("ap_name"))
    {
        ap_name = doc["ap_name"].as<String>();
        Serial.print("ap_name on sd card :");
        Serial.println(ap_name);
    }
    if (doc.containsKey("ap_password"))
    {
        ap_password = doc["ap_password"].as<String>();
        Serial.print("ap_password on sd card :");
        Serial.println(ap_password);
    }
    else if (doc.containsKey("password"))
    {
        ap_password = doc["password"].as<String>();
        Serial.print("legacy password reused as ap_password :");
        Serial.println(ap_password);
    }
    if (doc.containsKey("ap_ip_config"))
    {
        ap_ip_config = doc["ap_ip_config"].as<String>();
        Serial.print("ap_ip_config on sd card :");
        Serial.println(ap_ip_config);
    }
    else if (doc.containsKey("ap_ip"))
    {
        ap_ip_config = doc["ap_ip"].as<String>();
        Serial.print("legacy ap_ip reused as ap_ip_config :");
        Serial.println(ap_ip_config);
    }
    if (doc.containsKey("esp_now_channel"))
    {
        uint8_t tmp_channel = doc["esp_now_channel"].as<unsigned int>();
        if (tmp_channel >= 1 && tmp_channel <= 13)
        {
            esp_now_channel = tmp_channel;
            Serial.print("esp_now_channel on sd card :");
            Serial.println(esp_now_channel);
        }
    }
    if (doc.containsKey("device_mode"))
    {
        uint8_t tmp_mode = doc["device_mode"].as<unsigned int>();
        if (tmp_mode <= DEVICE_MODE_AP_STA_CLIENT)
        {
            device_mode = tmp_mode;
            Serial.print("device_mode on sd card :");
            Serial.println(device_mode);
        }
    }
    if (doc.containsKey("sta_ssid"))
    {
        sta_ssid = doc["sta_ssid"].as<String>();
        Serial.print("sta_ssid on sd card :");
        Serial.println(sta_ssid);
    }
    if (doc.containsKey("sta_password"))
    {
        sta_password = doc["sta_password"].as<String>();
        Serial.println("sta_password on sd card : (set)");
    }
    if (doc.containsKey("sta_use_dhcp"))
    {
        sta_use_dhcp = doc["sta_use_dhcp"].as<const bool>();
        Serial.printf("sta_use_dhcp on sd card : %s\n", sta_use_dhcp ? "true" : "false");
    }
    if (doc.containsKey("sta_ip_config"))
    {
        sta_ip_config = doc["sta_ip_config"].as<String>();
        Serial.print("sta_ip_config on sd card :");
        Serial.println(sta_ip_config);
    }
    if (doc.containsKey("sta_gateway_config"))
    {
        sta_gateway_config = doc["sta_gateway_config"].as<String>();
        Serial.print("sta_gateway_config on sd card :");
        Serial.println(sta_gateway_config);
    }
    if (doc.containsKey("sta_subnet_config"))
    {
        sta_subnet_config = doc["sta_subnet_config"].as<String>();
        Serial.print("sta_subnet_config on sd card :");
        Serial.println(sta_subnet_config);
    }
    if (doc.containsKey("mesh_ttl"))
    {
        uint8_t tmp_ttl = doc["mesh_ttl"].as<unsigned int>();
        if (tmp_ttl >= 1 && tmp_ttl <= MESH_MAX_TTL)
        {
            mesh_ttl = tmp_ttl;
            Serial.print("mesh_ttl on sd card :");
            Serial.println(mesh_ttl);
        }
    }
    if (doc.containsKey("ap_safety_timeout_s"))
    {
        int32_t tmp_timeout = doc["ap_safety_timeout_s"].as<int>();
        if (tmp_timeout >= 0 && tmp_timeout <= AP_SAFETY_TIMEOUT_MAX_S)
        {
            ap_safety_timeout_s = (uint16_t)tmp_timeout;
            Serial.print("ap_safety_timeout_s on sd card :");
            Serial.println(ap_safety_timeout_s);
        }
    }
    if (doc.containsKey("allow_play_over_playing"))
    {
        allow_play_over_playing = doc["allow_play_over_playing"].as<const bool>();
        Serial.print("allow_play_over_playing on sd card :");
        Serial.println(allow_play_over_playing ? "true" : "false");
    }
    if (doc.containsKey("button_gpio13_track"))
    {
        button_gpio13_track = doc["button_gpio13_track"].as<int>();
        Serial.print("button_gpio13_track on sd card :");
        Serial.println(button_gpio13_track);
    }
    else if (doc.containsKey("button_gpio14_track"))
    {
        button_gpio13_track = doc["button_gpio14_track"].as<int>();
        Serial.print("button_gpio14_track on sd card :");
        Serial.println(button_gpio13_track);
    }
    if (doc.containsKey("button_gpio16_track"))
    {
        button_gpio16_track = doc["button_gpio16_track"].as<int>();
        Serial.print("button_gpio16_track on sd card :");
        Serial.println(button_gpio16_track);
    }
    if (doc.containsKey("button_gpio13_pull_mode"))
    {
        button_gpio13_pull_mode = doc["button_gpio13_pull_mode"].as<unsigned int>();
        Serial.print("button_gpio13_pull_mode on sd card :");
        Serial.println(button_gpio13_pull_mode);
    }
    else if (doc.containsKey("button_gpio14_pull_mode"))
    {
        button_gpio13_pull_mode = doc["button_gpio14_pull_mode"].as<unsigned int>();
        Serial.print("button_gpio14_pull_mode on sd card :");
        Serial.println(button_gpio13_pull_mode);
    }
    if (doc.containsKey("button_gpio16_pull_mode"))
    {
        button_gpio16_pull_mode = doc["button_gpio16_pull_mode"].as<unsigned int>();
        Serial.print("button_gpio16_pull_mode on sd card :");
        Serial.println(button_gpio16_pull_mode);
    }
    if (doc.containsKey("button_gpio13_active_level"))
    {
        button_gpio13_active_level = doc["button_gpio13_active_level"].as<unsigned int>();
        Serial.print("button_gpio13_active_level on sd card :");
        Serial.println(button_gpio13_active_level);
    }
    else if (doc.containsKey("button_gpio14_active_level"))
    {
        button_gpio13_active_level = doc["button_gpio14_active_level"].as<unsigned int>();
        Serial.print("button_gpio14_active_level on sd card :");
        Serial.println(button_gpio13_active_level);
    }
    if (doc.containsKey("button_gpio16_active_level"))
    {
        button_gpio16_active_level = doc["button_gpio16_active_level"].as<unsigned int>();
        Serial.print("button_gpio16_active_level on sd card :");
        Serial.println(button_gpio16_active_level);
    }
    if (doc.containsKey("pcf_pull_mode"))
    {
        pcf_pull_mode = doc["pcf_pull_mode"].as<unsigned int>();
        Serial.print("pcf_pull_mode on sd card :");
        Serial.println(pcf_pull_mode);
    }
    if (doc.containsKey("pcf_active_level"))
    {
        pcf_active_level = doc["pcf_active_level"].as<unsigned int>();
        Serial.print("pcf_active_level on sd card :");
        Serial.println(pcf_active_level);
    }
    if (doc.containsKey("pcf_track_map"))
    {
        JsonArray pcfMap = doc["pcf_track_map"].as<JsonArray>();
        for (uint8_t i = 0; i < PCF_CHANNEL_COUNT; i++)
        {
            if (i < pcfMap.size())
            {
                pcf_track_map[i] = pcfMap[i].as<int>();
            }
        }
        Serial.println("pcf_track_map loaded from sd card");
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
    std::vector<t_music_data> previous_order = music_data;
    music_data.clear();

    for (std::vector<t_music_data>::size_type i = 0; i != previous_order.size(); i++)
    {
        const String &candidate = previous_order[i].path;
        if (std::find(files_list.begin(), files_list.end(), candidate) != files_list.end())
        {
            bool already_added = false;
            for (std::vector<t_music_data>::size_type j = 0; j != music_data.size(); j++)
            {
                if (music_data[j].path == candidate)
                {
                    already_added = true;
                    break;
                }
            }
            if (!already_added)
            {
                music_data.push_back((t_music_data){.path = candidate, .index = 0});
            }
        }
    }

    for (std::vector<String>::size_type i = 0; i != files_list.size(); i++)
    {
        bool already_added = false;
        for (std::vector<t_music_data>::size_type j = 0; j != music_data.size(); j++)
        {
            if (music_data[j].path == files_list[i])
            {
                already_added = true;
                break;
            }
        }
        if (!already_added)
        {
            music_data.push_back((t_music_data){.path = files_list[i], .index = 0});
        }
    }

    for (std::vector<t_music_data>::size_type i = 0; i != music_data.size(); i++)
    {
        music_data[i].index = (uint8_t)i;
    }
}

void apply_reordered_paths(const std::vector<String> &ordered_paths)
{
    if (ordered_paths.size() == 0 || music_data.size() == 0)
        return;

    std::vector<t_music_data> reordered;
    reordered.reserve(music_data.size());

    for (std::vector<String>::size_type i = 0; i != ordered_paths.size(); i++)
    {
        for (std::vector<t_music_data>::size_type j = 0; j != music_data.size(); j++)
        {
            if (music_data[j].path == ordered_paths[i])
            {
                bool already_added = false;
                for (std::vector<t_music_data>::size_type k = 0; k != reordered.size(); k++)
                {
                    if (reordered[k].path == music_data[j].path)
                    {
                        already_added = true;
                        break;
                    }
                }
                if (!already_added)
                {
                    reordered.push_back((t_music_data){.path = music_data[j].path, .index = 0});
                }
            }
        }
    }

    for (std::vector<t_music_data>::size_type i = 0; i != music_data.size(); i++)
    {
        bool already_added = false;
        for (std::vector<t_music_data>::size_type j = 0; j != reordered.size(); j++)
        {
            if (reordered[j].path == music_data[i].path)
            {
                already_added = true;
                break;
            }
        }
        if (!already_added)
        {
            reordered.push_back((t_music_data){.path = music_data[i].path, .index = 0});
        }
    }

    music_data = reordered;
    for (std::vector<t_music_data>::size_type i = 0; i != music_data.size(); i++)
    {
        music_data[i].index = (uint8_t)i;
    }
}

String build_ap_ssid()
{
    String prefix = ap_name;
    prefix.trim();
    if (prefix.length() == 0)
        prefix = "I2S-SD-DEFAULT";
    if (prefix.length() > 31)
        prefix = prefix.substring(0, 31);
    return prefix;
}

String resolve_ap_ssid()
{
    String configured_ssid = ap_ssid;
    configured_ssid.trim();
    if (configured_ssid.length() > 31)
        configured_ssid = configured_ssid.substring(0, 31);
    if (configured_ssid.length() > 0)
        return configured_ssid;
    return build_ap_ssid();
}

void mark_esp_now_activity()
{
    last_esp_now_activity_ms = millis();
}

bool parse_ipv4_string(const String &ip_value, IPAddress &parsed_ip)
{
    int octet1 = 0;
    int octet2 = 0;
    int octet3 = 0;
    int octet4 = 0;
    if (sscanf(ip_value.c_str(), "%d.%d.%d.%d", &octet1, &octet2, &octet3, &octet4) != 4)
    {
        return false;
    }
    if (octet1 < 0 || octet1 > 255 || octet2 < 0 || octet2 > 255 || octet3 < 0 || octet3 > 255 || octet4 < 0 || octet4 > 255)
    {
        return false;
    }
    parsed_ip = IPAddress((uint8_t)octet1, (uint8_t)octet2, (uint8_t)octet3, (uint8_t)octet4);
    return true;
}

bool is_ap_enabled_mode(uint8_t mode)
{
    return mode == DEVICE_MODE_CURRENT || mode == DEVICE_MODE_MESH || mode == DEVICE_MODE_AP_STA_CLIENT;
}

bool is_mesh_mode(uint8_t mode)
{
    return mode == DEVICE_MODE_MESH;
}

bool is_relay_mode(uint8_t mode)
{
    return mode == DEVICE_MODE_MESH || mode == DEVICE_MODE_RELAY_ONLY;
}

bool start_soft_ap_runtime()
{
    IPAddress configured_ap_ip;
    parse_ipv4_string(ap_ip_config, configured_ap_ip);
    IPAddress ap_subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    if (!WiFi.softAPConfig(configured_ap_ip, configured_ap_ip, ap_subnet))
    {
        Serial.println("AP IP config failed, continuing with defaults");
    }

    ap_ssid = resolve_ap_ssid();
    bool ap_started = WiFi.softAP(ap_ssid.c_str(), ap_password.c_str(), esp_now_channel, false, 4);
    if (!ap_started)
    {
        Serial.println("AP initialization failed!");
        ap_runtime_enabled = false;
        return false;
    }
    ap_ip = WiFi.softAPIP().toString();
    ap_runtime_enabled = true;
    return true;
}

bool connect_wifi_sta_runtime(uint16_t timeout_ms = 15000)
{
    sta_ssid.trim();
    if (sta_ssid.length() == 0)
    {
        sta_connected = false;
        sta_ip = "";
        Serial.println("STA SSID is empty, skipping router connection");
        return false;
    }
    if (sta_ssid.length() > 31)
    {
        sta_ssid = sta_ssid.substring(0, 31);
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);

    if (!sta_use_dhcp)
    {
        IPAddress sta_ip_addr;
        IPAddress sta_gateway;
        IPAddress sta_subnet;
        if (!parse_ipv4_string(sta_ip_config, sta_ip_addr) ||
            !parse_ipv4_string(sta_gateway_config, sta_gateway) ||
            !parse_ipv4_string(sta_subnet_config, sta_subnet))
        {
            Serial.println("Invalid static STA IP config, falling back to DHCP");
            sta_use_dhcp = true;
        }
        else
        {
            WiFi.config(sta_ip_addr, sta_gateway, sta_subnet);
        }
    }

    Serial.printf("Connecting STA to \"%s\"...\n", sta_ssid.c_str());
    WiFi.begin(sta_ssid.c_str(), sta_password.c_str());

    uint32_t start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - start_ms) < timeout_ms)
    {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        sta_connected = true;
        sta_ip = WiFi.localIP().toString();
        uint8_t router_channel = WiFi.channel();
        if (router_channel >= 1 && router_channel <= 13)
        {
            esp_now_channel = router_channel;
            esp_err_t channel_result = esp_wifi_set_channel(esp_now_channel, WIFI_SECOND_CHAN_NONE);
            if (channel_result != ESP_OK)
            {
                Serial.printf("Failed to sync ESP-NOW channel to STA: %d\n", channel_result);
            }
        }
        Serial.printf("STA connected, IP: %s, channel: %u\n", sta_ip.c_str(), esp_now_channel);
        return true;
    }

    sta_connected = false;
    sta_ip = "";
    Serial.println("STA connection failed (AP remains active)");
    return false;
}

uint32_t generate_esp_now_message_id()
{
    uint32_t next_id = esp_now_message_counter++;
    if (next_id == 0)
    {
        next_id = esp_now_message_counter++;
    }
    return next_id;
}

bool has_seen_mesh_message(uint32_t origin_id, uint32_t message_id)
{
    for (uint8_t i = 0; i < MESH_SEEN_CACHE_SIZE; i++)
    {
        if (mesh_seen_messages[i].origin_id == origin_id && mesh_seen_messages[i].message_id == message_id)
        {
            return true;
        }
    }
    return false;
}

void remember_mesh_message(uint32_t origin_id, uint32_t message_id)
{
    mesh_seen_messages[mesh_seen_cursor] = (t_mesh_seen_message){.origin_id = origin_id, .message_id = message_id};
    mesh_seen_cursor = (mesh_seen_cursor + 1) % MESH_SEEN_CACHE_SIZE;
}

void sanitize_network_settings()
{
    ap_ssid.trim();
    if (ap_ssid.length() > 31)
    {
        ap_ssid = ap_ssid.substring(0, 31);
    }

    ap_name.trim();
    if (ap_name.length() == 0)
    {
        ap_name = "I2S-SD-DEFAULT";
    }

    if (device_mode > DEVICE_MODE_AP_STA_CLIENT)
    {
        device_mode = DEVICE_MODE_CURRENT;
    }

    sta_ssid.trim();
    if (sta_ssid.length() > 31)
    {
        sta_ssid = sta_ssid.substring(0, 31);
    }
    sta_password.trim();
    if (sta_password.length() > 63)
    {
        sta_password = sta_password.substring(0, 63);
    }
    sta_ip_config.trim();
    sta_gateway_config.trim();
    sta_subnet_config.trim();
    if (sta_subnet_config.length() == 0)
    {
        sta_subnet_config = "255.255.255.0";
    }
    if (device_mode == DEVICE_MODE_AP_STA_CLIENT && sta_ssid.length() == 0)
    {
        Serial.println("Warning: device_mode 4 requires sta_ssid to connect to router");
    }

    if (mesh_ttl < 1 || mesh_ttl > MESH_MAX_TTL)
    {
        mesh_ttl = MESH_DEFAULT_TTL;
    }
    if (button_gpio13_track < -1)
    {
        button_gpio13_track = -1;
    }
    if (button_gpio16_track < -1)
    {
        button_gpio16_track = -1;
    }
    if (button_gpio13_pull_mode > BUTTON_PULL_MODE_NONE)
    {
        button_gpio13_pull_mode = BUTTON_PULL_MODE_UP;
    }
    if (button_gpio16_pull_mode > BUTTON_PULL_MODE_NONE)
    {
        button_gpio16_pull_mode = BUTTON_PULL_MODE_UP;
    }
    if (button_gpio13_active_level > BUTTON_ACTIVE_LEVEL_HIGH)
    {
        button_gpio13_active_level = BUTTON_ACTIVE_LEVEL_LOW;
    }
    if (button_gpio16_active_level > BUTTON_ACTIVE_LEVEL_HIGH)
    {
        button_gpio16_active_level = BUTTON_ACTIVE_LEVEL_LOW;
    }
    if (pcf_pull_mode > PCF_PULL_MODE_DOWN)
    {
        pcf_pull_mode = PCF_PULL_MODE_UP;
    }
    if (pcf_active_level > PCF_ACTIVE_LEVEL_HIGH)
    {
        pcf_active_level = PCF_ACTIVE_LEVEL_LOW;
    }
    for (uint8_t i = 0; i < PCF_CHANNEL_COUNT; i++)
    {
        if (pcf_track_map[i] < -1)
        {
            pcf_track_map[i] = -1;
        }
    }
    if (led_mode > 3)
    {
        led_mode = 0;
    }

    ap_password.trim();
    if (ap_password.length() < 8)
    {
        Serial.println("AP password too short, fallback to 12345678");
        ap_password = "12345678";
    }
    if (ap_password.length() > 63)
    {
        ap_password = ap_password.substring(0, 63);
    }

    if (esp_now_channel < 1 || esp_now_channel > 13)
    {
        esp_now_channel = 6;
    }
    if (ap_safety_timeout_s > AP_SAFETY_TIMEOUT_MAX_S)
    {
        ap_safety_timeout_s = AP_SAFETY_TIMEOUT_MAX_S;
    }

    IPAddress parsed_ip;
    if (!parse_ipv4_string(ap_ip_config, parsed_ip))
    {
        Serial.println("Invalid AP IP, fallback to 192.168.4.1");
        ap_ip_config = "192.168.4.1";
    }

    if (!sta_use_dhcp && device_mode == DEVICE_MODE_AP_STA_CLIENT)
    {
        IPAddress tmp;
        if (!parse_ipv4_string(sta_ip_config, tmp) ||
            !parse_ipv4_string(sta_gateway_config, tmp) ||
            !parse_ipv4_string(sta_subnet_config, tmp))
        {
            Serial.println("Invalid STA static IP settings, enabling DHCP");
            sta_use_dhcp = true;
        }
    }
}

void schedule_restart(uint32_t delay_ms)
{
    restart_requested = true;
    restart_requested_at_ms = millis() + delay_ms;
}

void handle_pending_restart()
{
    if (!restart_requested)
    {
        return;
    }
    if ((int32_t)(millis() - restart_requested_at_ms) >= 0)
    {
        Serial.println("Applying network changes, restarting...");
        delay(100);
        ESP.restart();
    }
}

void handle_ap_off_safety_timeout()
{
    // If safety AP is on, turn it off after the configured duration (e.g. 5 min boot window)
    if (ap_safety_ap_activated && ap_safety_activated_at_ms != 0)
    {
        uint32_t elapsed_ms = (uint32_t)(millis() - ap_safety_activated_at_ms);
        if (elapsed_ms >= (uint32_t)ap_safety_timeout_s * 1000UL)
        {
            Serial.printf("AP safety window ended (%us), disabling AP\n", ap_safety_timeout_s);
            WiFi.softAPdisconnect(false);
            ap_safety_ap_activated = false;
            ap_runtime_enabled = false;
            ap_ip = "";
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            esp_err_t channel_result = esp_wifi_set_channel(esp_now_channel, WIFI_SECOND_CHAN_NONE);
            if (channel_result != ESP_OK)
            {
                Serial.printf("Failed to set ESP-NOW channel: %d\n", channel_result);
            }
            return;
        }
        return;
    }

    if (!esp_now_ready || is_ap_enabled_mode(device_mode) || ap_runtime_enabled || ap_safety_ap_activated)
    {
        return;
    }
    if (ap_safety_timeout_s == 0)
    {
        return;
    }
    uint32_t timeout_ms = (uint32_t)ap_safety_timeout_s * 1000UL;
    if ((uint32_t)(millis() - last_esp_now_activity_ms) < timeout_ms)
    {
        return;
    }

    Serial.printf("ESP-NOW safety timeout reached (%us), enabling AP fallback\n", ap_safety_timeout_s);
    if (start_soft_ap_runtime())
    {
        ap_safety_ap_activated = true;
        ap_safety_activated_at_ms = millis();
        update_spiffs();
    }
}

void stop_playback()
{
    audio.stopSong();
    audioSource = AUDIO_SOURCE_NONE;
    stream_url = "";
    Serial.println("Playback stopped");
}

bool play_stream_by_url(const String &url)
{
    if (url.length() == 0)
    {
        Serial.println("Stream URL is empty");
        return false;
    }
    if (!allow_play_over_playing && audio.isRunning())
    {
        Serial.println("Stream ignored: audio already playing (allow_play_over_playing is false)");
        return false;
    }
    Serial.printf("Streaming: %s\n", url.c_str());
    audio.setFileLoop(false);
    bool ok = audio.connecttohost(url.c_str());
    if (ok)
    {
        audioSource = AUDIO_SOURCE_STREAM;
        stream_url = url;
    }
    else
    {
        Serial.println("connecttohost failed");
    }
    return ok;
}

String stream_status_json()
{
    const char *source = "none";
    if (audioSource == AUDIO_SOURCE_SD)
        source = "sd";
    else if (audioSource == AUDIO_SOURCE_STREAM)
        source = "stream";

    DynamicJsonDocument doc(512);
    doc["source"] = source;
    doc["running"] = audio.isRunning();
    doc["url"] = stream_url;
    doc["firmware"] = "1.3-wifi-stream";
    doc["device_id"] = device_id;
    doc["ap_name"] = ap_name;
    doc["udp_port"] = localPort;
    String out;
    serializeJson(doc, out);
    return out;
}

void send_udp_discovery_response()
{
    IPAddress local_ip;
    if (sta_connected && sta_ip.length() > 0)
    {
        local_ip.fromString(sta_ip);
    }
    else
    {
        local_ip = WiFi.softAPIP();
        if (local_ip == IPAddress(0, 0, 0, 0))
            local_ip = WiFi.localIP();
    }

    DynamicJsonDocument doc(384);
    doc["ap_name"] = ap_name;
    doc["device_id"] = device_id;
    doc["ip"] = local_ip.toString();
    doc["udp_port"] = localPort;
    doc["firmware"] = "1.3-wifi-stream";
    String payload;
    serializeJson(doc, payload);

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)payload.c_str(), payload.length());
    udp.endPacket();
    Serial.printf("Discovery response sent: %s\n", payload.c_str());
}

String sanitize_mdns_hostname(const String &name)
{
    String host = name;
    host.trim();
    if (host.length() == 0)
        host = "esp32audio";
    for (size_t i = 0; i < host.length(); ++i)
    {
        char c = host.charAt(i);
        if (!isalnum(c) && c != '-')
            host.setCharAt(i, '-');
    }
    return host;
}

void init_mdns()
{
    String host = sanitize_mdns_hostname(ap_name);
    if (!MDNS.begin(host.c_str()))
    {
        Serial.println("mDNS init failed");
        return;
    }
    MDNS.addService("esp32audio", "udp", localPort);
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS started: %s.local\n", host.c_str());
}

bool play_track_by_index(uint16_t audio_to_play)
{
    if (!allow_play_over_playing && audio.isRunning())
    {
        Serial.println("Play ignored: a track is already playing (allow_play_over_playing is false)");
        return false;
    }
    if (music_data.size() > audio_to_play)
    {
        String target_path = music_data[audio_to_play].path;
        if (!SD.exists(target_path))
        {
            update_music_from_sd();
            if (music_data.size() <= audio_to_play)
            {
                Serial.printf("Sound number %d is out of range\n", audio_to_play);
                return false;
            }
            target_path = music_data[audio_to_play].path;
        }

        Serial.printf("Playing : %s\n", target_path.c_str());
        audio.connecttoFS(SD, target_path.c_str());
        audioSource = AUDIO_SOURCE_SD;
        stream_url = "";
        if (loop_file)
        {
            audio.setFileLoop(true);
        }
        return true;
    }
    Serial.printf("Sound number %d is out of range\n", audio_to_play);
    return false;
}

bool should_skip_remote_duplicate_play(uint16_t track_index)
{
    uint32_t now = millis();
    if (last_remote_played_track == track_index &&
        (uint32_t)(now - last_remote_played_at_ms) < REMOTE_PLAYBACK_DEDUP_WINDOW_MS)
    {
        return true;
    }
    last_remote_played_track = track_index;
    last_remote_played_at_ms = now;
    return false;
}

int16_t get_track_for_button(uint8_t gpio)
{
    if (gpio == BUTTON_GPIO_13)
        return button_gpio13_track;
    if (gpio == BUTTON_GPIO_16)
        return button_gpio16_track;
    return -1;
}

uint8_t get_pull_mode_for_button(uint8_t gpio)
{
    if (gpio == BUTTON_GPIO_13)
        return button_gpio13_pull_mode;
    if (gpio == BUTTON_GPIO_16)
        return button_gpio16_pull_mode;
    return BUTTON_PULL_MODE_UP;
}

uint8_t get_active_level_for_button(uint8_t gpio)
{
    if (gpio == BUTTON_GPIO_13)
        return button_gpio13_active_level;
    if (gpio == BUTTON_GPIO_16)
        return button_gpio16_active_level;
    return BUTTON_ACTIVE_LEVEL_LOW;
}

bool is_button_active_level(uint8_t gpio, bool level)
{
    uint8_t active_level = get_active_level_for_button(gpio);
    if (active_level == BUTTON_ACTIVE_LEVEL_HIGH)
        return level == HIGH;
    return level == LOW;
}

void apply_button_input_config()
{
    for (uint8_t i = 0; i < 2; i++)
    {
        t_button_state &button = button_states[i];
        uint8_t pull_mode = get_pull_mode_for_button(button.gpio);
        if (pull_mode == BUTTON_PULL_MODE_DOWN)
            pinMode(button.gpio, INPUT_PULLDOWN);
        else if (pull_mode == BUTTON_PULL_MODE_NONE)
            pinMode(button.gpio, INPUT);
        else
            pinMode(button.gpio, INPUT_PULLUP);

        bool reading = digitalRead(button.gpio);
        button.stable_state = reading;
        button.last_reading = reading;
        button.last_change_ms = millis();
    }
}

void on_esp_now_receive(const uint8_t *mac_addr, const uint8_t *incoming_data, int len)
{
    (void)mac_addr;
    t_esp_now_packet packet = {};
    uint8_t packet_format = 0;

    if (len == (int)sizeof(t_esp_now_packet))
    {
        t_esp_now_packet candidate = {};
        memcpy(&candidate, incoming_data, sizeof(candidate));
        if (candidate.magic != ESP_NOW_PACKET_MAGIC || candidate.version != ESP_NOW_PACKET_VERSION || candidate.cmd != ESP_NOW_CMD_PLAY_TRACK)
            return;
        packet = candidate;
        packet_format = ESP_NOW_PACKET_VERSION;
    }
    else if (len == (int)sizeof(t_esp_now_packet_legacy))
    {
        t_esp_now_packet_legacy legacy = {};
        memcpy(&legacy, incoming_data, sizeof(legacy));
        if (legacy.magic != ESP_NOW_PACKET_MAGIC || legacy.version != ESP_NOW_PACKET_VERSION_LEGACY || legacy.cmd != ESP_NOW_CMD_PLAY_TRACK)
            return;
        packet = (t_esp_now_packet){
            .magic = legacy.magic,
            .version = legacy.version,
            .cmd = legacy.cmd,
            .ttl = 0,
            .track_index = legacy.track_index,
            .origin_id = 0,
            .message_id = 0};
        packet_format = ESP_NOW_PACKET_VERSION_LEGACY;
    }
    else
    {
        return;
    }

    portENTER_CRITICAL_ISR(&esp_now_mux);
    esp_now_packet_to_handle = packet;
    esp_now_packet_format_to_handle = packet_format;
    esp_now_pending_packet = true;
    portEXIT_CRITICAL_ISR(&esp_now_mux);
}

bool init_esp_now()
{
    esp_now_ready = false;

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return false;
    }
    esp_now_register_recv_cb(on_esp_now_receive);

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, ESP_NOW_BROADCAST_ADDR, sizeof(ESP_NOW_BROADCAST_ADDR));
    peer_info.channel = esp_now_channel;
    peer_info.encrypt = false;

    esp_err_t add_peer_result = esp_now_add_peer(&peer_info);
    if (add_peer_result != ESP_OK && add_peer_result != ESP_ERR_ESPNOW_EXIST)
    {
        Serial.printf("ESP-NOW add peer error: %d\n", add_peer_result);
        return false;
    }
    for (uint8_t i = 0; i < MESH_SEEN_CACHE_SIZE; i++)
    {
        mesh_seen_messages[i] = (t_mesh_seen_message){.origin_id = 0, .message_id = 0};
    }
    mesh_seen_cursor = 0;
    esp_now_ready = true;
    Serial.println("ESP-NOW initialized");
    return true;
}

void send_esp_now_packet(const uint8_t *packet_data, size_t packet_len)
{
    if (!esp_now_ready)
        return;

    esp_err_t send_result = esp_now_send(ESP_NOW_BROADCAST_ADDR, packet_data, packet_len);
    if (send_result != ESP_OK)
    {
        Serial.printf("ESP-NOW send failed: %d\n", send_result);
    }
    else
    {
        mark_esp_now_activity();
    }
}

void send_esp_now_play_track_legacy(uint16_t track_index)
{
    t_esp_now_packet_legacy packet = {
        .magic = ESP_NOW_PACKET_MAGIC,
        .version = ESP_NOW_PACKET_VERSION_LEGACY,
        .cmd = ESP_NOW_CMD_PLAY_TRACK,
        .reserved = 0,
        .track_index = track_index};
    send_esp_now_packet((const uint8_t *)&packet, sizeof(packet));
}

void send_esp_now_play_track(uint16_t track_index)
{
    // Compatibility path: keep current mode interoperable with legacy firmware.
    if (!is_relay_mode(device_mode))
    {
        send_esp_now_play_track_legacy(track_index);
        return;
    }

    t_esp_now_packet packet = {
        .magic = ESP_NOW_PACKET_MAGIC,
        .version = ESP_NOW_PACKET_VERSION,
        .cmd = ESP_NOW_CMD_PLAY_TRACK,
        .ttl = (uint8_t)(is_relay_mode(device_mode) ? mesh_ttl : 0),
        .track_index = track_index,
        .origin_id = device_id,
        .message_id = generate_esp_now_message_id()};

    remember_mesh_message(packet.origin_id, packet.message_id);
    send_esp_now_packet((const uint8_t *)&packet, sizeof(packet));
    // Also send legacy packet so mixed fleets can still play in relay mode.
    send_esp_now_play_track_legacy(track_index);
}

void handle_button_pressed(uint8_t gpio)
{
    int16_t configured_track = get_track_for_button(gpio);
    if (configured_track < 0)
    {
        Serial.printf("GPIO %u: no track assigned\n", gpio);
        return;
    }
    Serial.printf("GPIO %u pressed, track %d\n", gpio, configured_track);
    if (play_track_by_index((uint16_t)configured_track))
    {
        send_esp_now_play_track((uint16_t)configured_track);
    }
}

void handle_pcf_pending_interrupt()
{
    if (!pcfReady || !pcfInterruptPending)
    {
        return;
    }
    pcfInterruptPending = false;

    uint16_t pcfValue = 0;
    for (uint8_t pin = 0; pin < 16; ++pin)
    {
        if (PCF.read(pin))
        {
            pcfValue |= (1u << pin);
        }
    }
    uint16_t changed = pcfValue ^ pcfPrevValue;
    uint16_t activeEdges = 0;
    if (pcf_active_level == PCF_ACTIVE_LEVEL_HIGH)
    {
        activeEdges = changed & pcfValue;
    }
    else
    {
        activeEdges = changed & (~pcfValue);
    }
    pcfPrevValue = pcfValue;
    if (!activeEdges)
    {
        return;
    }
    for (uint8_t pin = 0; pin < PCF_CHANNEL_COUNT; ++pin)
    {
        if ((activeEdges & (1u << pin)) == 0)
        {
            continue;
        }
        int16_t mappedTrack = pcf_track_map[pin];
        if (mappedTrack < 0)
        {
            Serial.printf("PCF channel %u disabled\n", pin);
            continue;
        }
        Serial.printf("PCF channel %u triggered -> track %d\n", pin, mappedTrack);
        if (play_track_by_index((uint16_t)mappedTrack))
        {
            send_esp_now_play_track((uint16_t)mappedTrack);
        }
        break;
    }
}

void poll_button_state(t_button_state &button, uint32_t now_ms)
{
    bool reading = digitalRead(button.gpio);

    if (reading != button.last_reading)
    {
        button.last_reading = reading;
        button.last_change_ms = now_ms;
    }

    if ((uint32_t)(now_ms - button.last_change_ms) >= BUTTON_DEBOUNCE_MS && button.stable_state != reading)
    {
        button.stable_state = reading;
        if (is_button_active_level(button.gpio, button.stable_state))
            handle_button_pressed(button.gpio);
    }
}

void poll_buttons()
{
    uint32_t now_ms = millis();
    poll_button_state(button_states[0], now_ms);
    poll_button_state(button_states[1], now_ms);
}

void handle_pending_esp_now_commands()
{
    if (!esp_now_pending_packet)
        return;

    t_esp_now_packet packet = {};
    uint8_t packet_format = 0;
    portENTER_CRITICAL(&esp_now_mux);
    packet = esp_now_packet_to_handle;
    packet_format = esp_now_packet_format_to_handle;
    esp_now_pending_packet = false;
    esp_now_packet_format_to_handle = 0;
    portEXIT_CRITICAL(&esp_now_mux);

    if (packet_format == ESP_NOW_PACKET_VERSION_LEGACY)
    {
        mark_esp_now_activity();
        Serial.printf("ESP-NOW legacy track received: %u\n", packet.track_index);
        if (should_skip_remote_duplicate_play(packet.track_index))
        {
            Serial.printf("Ignoring duplicate remote track (legacy): %u\n", packet.track_index);
        }
        else
        {
            play_track_by_index(packet.track_index);
        }
        return;
    }
    if (packet_format != ESP_NOW_PACKET_VERSION)
    {
        return;
    }

    if (packet.origin_id == device_id)
    {
        return;
    }
    mark_esp_now_activity();
    if (has_seen_mesh_message(packet.origin_id, packet.message_id))
    {
        return;
    }
    remember_mesh_message(packet.origin_id, packet.message_id);

    Serial.printf("ESP-NOW track received: %u (ttl:%u)\n", packet.track_index, packet.ttl);
    if (should_skip_remote_duplicate_play(packet.track_index))
    {
        Serial.printf("Ignoring duplicate remote track: %u\n", packet.track_index);
    }
    else
    {
        play_track_by_index(packet.track_index);
    }

    if (is_relay_mode(device_mode) && packet.ttl > 0)
    {
        t_esp_now_packet relay_packet = packet;
        relay_packet.ttl = packet.ttl - 1;
        delay(random(4, 15));
        send_esp_now_packet((const uint8_t *)&relay_packet, sizeof(relay_packet));
    }
}

void handleDelete(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    audio.stopSong();

    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, data, len);
    if (error)
    {
        Serial.println(error.c_str());
        return;
    }
    int file_index = doc["index"].as<int>();
    if (file_index < 0 || file_index >= (int)music_data.size())
    {
        request->send(400, "text/plain", "Invalid file index");
        return;
    }
    String path_to_remove = music_data[file_index].path;
    Serial.printf("Removing file %d (%s)\n", file_index, path_to_remove.c_str());
    SD.remove(path_to_remove.c_str());
    update_music_from_sd();
    update_spiffs();
    request->send(200);
}

void handleStop(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    audio.stopSong();
    request->send(200);
}

void handleReorder(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, data, len);
    if (error || !doc.containsKey("paths"))
    {
        request->send(400, "text/plain", "Invalid reorder payload");
        return;
    }

    JsonArray paths = doc["paths"].as<JsonArray>();
    if (paths.isNull() || paths.size() == 0)
    {
        request->send(400, "text/plain", "No paths provided");
        return;
    }

    std::vector<String> ordered_paths;
    ordered_paths.reserve(paths.size());
    for (JsonVariant value : paths)
    {
        String candidate = value.as<String>();
        if (candidate.length() > 0)
        {
            ordered_paths.push_back(candidate);
        }
    }
    if (ordered_paths.size() == 0)
    {
        request->send(400, "text/plain", "No valid paths provided");
        return;
    }

    update_music_from_sd();
    apply_reordered_paths(ordered_paths);
    update_spiffs();
    request->send(200, "application/json", local_vars_to_json());
}

void handleSimulateButton(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, data, len);
    if (error || !doc.containsKey("gpio"))
    {
        request->send(400, "text/plain", "Invalid simulate payload");
        return;
    }

    int gpio = doc["gpio"].as<int>();
    if (gpio != BUTTON_GPIO_13 && gpio != BUTTON_GPIO_16)
    {
        request->send(400, "text/plain", "Unsupported GPIO");
        return;
    }

    handle_button_pressed((uint8_t)gpio);
    request->send(200);
}

void handleSettings(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    String previous_ap_ssid = ap_ssid;
    String previous_ap_name = ap_name;
    String previous_ap_password = ap_password;
    String previous_ap_ip_config = ap_ip_config;
    String previous_sta_ssid = sta_ssid;
    String previous_sta_password = sta_password;
    bool previous_sta_use_dhcp = sta_use_dhcp;
    String previous_sta_ip_config = sta_ip_config;
    String previous_sta_gateway_config = sta_gateway_config;
    String previous_sta_subnet_config = sta_subnet_config;
    uint8_t previous_device_mode = device_mode;
    uint16_t previous_ap_safety_timeout_s = ap_safety_timeout_s;
    uint8_t previous_channel = esp_now_channel;
    unsigned int previous_udp_port = localPort;

    Serial.printf("Handle settings body size: %u\n", len);
    json_to_local_vars(data, len);
    sanitize_network_settings();
    apply_button_input_config();
    apply_pcf_input_config();
    update_spiffs();

    bool network_changed = previous_ap_ssid != ap_ssid ||
                           previous_ap_name != ap_name ||
                           previous_ap_password != ap_password ||
                           previous_ap_ip_config != ap_ip_config ||
                           previous_sta_ssid != sta_ssid ||
                           previous_sta_password != sta_password ||
                           previous_sta_use_dhcp != sta_use_dhcp ||
                           previous_sta_ip_config != sta_ip_config ||
                           previous_sta_gateway_config != sta_gateway_config ||
                           previous_sta_subnet_config != sta_subnet_config ||
                           previous_device_mode != device_mode ||
                           previous_ap_safety_timeout_s != ap_safety_timeout_s ||
                           previous_channel != esp_now_channel ||
                           previous_udp_port != localPort;

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
    if (network_changed)
    {
        schedule_restart(700);
    }
}

void handlePlay(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, data, len);
    if (error)
    {
        Serial.println(error.c_str());
        return;
    }
    int file_index = doc["index"].as<unsigned int>();
    Serial.printf("Playing file %d\n", file_index);
    play_track_by_index(file_index);
    request->send(200);
}
void handleRequest(AsyncWebServerRequest *request)
{
    String filePath = request->url(); // Obtenez l'URL demandée
    int query_index = filePath.indexOf('?');
    if (query_index >= 0)
    {
        filePath = filePath.substring(0, query_index);
    }
    filePath = decode_url_path(filePath);

    // Vérifier si le fichier existe sur la carte SD
    if (SD.exists(filePath))
    {
        String contentType = getContentType(filePath);
        request->send(SD, filePath, contentType, true);
        return;
    }

    // Si le fichier n'existe pas ou s'il y a une erreur, renvoyer une réponse 404
    request->send(404, "text/plain", "File not found on sd card");
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
        update_spiffs();
        request->send(200);
    }
}

bool has_web_serial_provision_marker()
{
    return SPIFFS.exists(WEB_SERIAL_PROVISION_STATE_FILE);
}

void mark_web_serial_provision_done()
{
    fs::File marker = SPIFFS.open(WEB_SERIAL_PROVISION_STATE_FILE, "w");
    if (!marker)
    {
        return;
    }
    marker.print("done");
    marker.close();
}

bool apply_web_serial_config_json(const String &json_payload)
{
    DynamicJsonDocument validation_doc(SETTINGS_DOC_CAPACITY);
    DeserializationError validation_error = deserializeJson(validation_doc, json_payload);
    if (validation_error)
    {
        Serial.printf("WEBCFG:INVALID:%s\n", validation_error.c_str());
        return false;
    }

    json_to_local_vars((const uint8_t *)json_payload.c_str(), json_payload.length());
    sanitize_network_settings();
    apply_button_input_config();
    apply_pcf_input_config();
    update_spiffs();
    return true;
}

void handle_first_boot_serial_provisioning()
{
    if (has_web_serial_provision_marker())
    {
        return;
    }

    Serial.println("WEBCFG:WINDOW_START");
    uint32_t deadline_ms = millis() + WEB_SERIAL_PROVISION_WINDOW_MS;
    uint32_t last_ready_ms = 0;
    String serial_line = "";
    serial_line.reserve(256);
    bool config_received = false;
    bool config_changed = false;

    while ((int32_t)(millis() - deadline_ms) < 0)
    {
        uint32_t now_ms = millis();
        if (now_ms - last_ready_ms >= WEB_SERIAL_PROVISION_READY_INTERVAL_MS)
        {
            Serial.printf("WEBCFG:READY:%lu\n", (unsigned long)WEB_SERIAL_PROVISION_WINDOW_MS);
            last_ready_ms = now_ms;
        }

        while (Serial.available() > 0)
        {
            char c = (char)Serial.read();
            if (c == '\r')
                continue;

            if (c == '\n')
            {
                String received_line = serial_line;
                serial_line = "";
                received_line.trim();

                if (!received_line.startsWith(WEB_SERIAL_PROVISION_PREFIX))
                {
                    continue;
                }

                String payload = received_line.substring(strlen(WEB_SERIAL_PROVISION_PREFIX));
                payload.trim();
                String previous_json = local_vars_to_json();
                config_received = apply_web_serial_config_json(payload);
                if (config_received)
                {
                    config_changed = local_vars_to_json() != previous_json;
                    if (config_changed)
                    {
                        Serial.println("WEBCFG:APPLIED");
                    }
                    else
                    {
                        Serial.println("WEBCFG:NO_CHANGE");
                    }
                }
                break;
            }

            if (serial_line.length() < WEB_SERIAL_PROVISION_MAX_LINE)
            {
                serial_line += c;
            }
        }

        if (config_received)
        {
            break;
        }
        delay(5);
    }

    mark_web_serial_provision_done();
    if (config_changed)
    {
        delay(120);
        ESP.restart();
        return;
    }
    if (config_received)
    {
        Serial.println("WEBCFG:DONE");
        return;
    }
    Serial.println("WEBCFG:SKIP");
}

void setup()
{
    pinMode((int)START_BUTTON, INPUT_PULLUP);
    esp_sleep_enable_ext0_wakeup(START_BUTTON, 0);
    rtc_gpio_pulldown_dis((gpio_num_t)START_BUTTON);
    rtc_gpio_pullup_en((gpio_num_t)START_BUTTON);
    pinMode(I2S_ENABLE, OUTPUT);
    digitalWrite(I2S_ENABLE, HIGH);
    pinMode(CHRG_STATUS, INPUT);
    pinMode(USB_VOLTAGE_PIN, INPUT);
    pinMode(BATTERY_VOLTAGE_PIN, INPUT);
    analogSetWidth(12);
    analogSetPinAttenuation(USB_VOLTAGE_PIN, ADC_11db);
    analogSetPinAttenuation(BATTERY_VOLTAGE_PIN, ADC_11db);
    strip.begin();
    strip.setBrightness(led_brightness);
    strip.clear();
    strip.show();

    pinMode(SD_CS, OUTPUT);
    pinMode(2, OUTPUT); ///
    digitalWrite(2, 1); ///
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    Serial.begin(115200);
    Wire.begin(21, 22);
    Wire.setClock(100000);
    if (!PCF.begin())
    {
        Serial.println("PCF8575 init failed");
        pcfReady = false;
    }
    else
    {
        pcfReady = true;
        apply_pcf_input_config();
    }
    pinMode(PCF_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PCF_INT_PIN), onPcfInterrupt, FALLING);
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
    handle_first_boot_serial_provisioning();
    Serial.printf("JSON : %s/n", local_vars_to_json().c_str());

    uint64_t chip_id = ESP.getEfuseMac();
    device_id = (uint32_t)(chip_id & 0xFFFFFFFF);
    if (device_id == 0)
    {
        device_id = 1;
    }
    esp_now_message_counter = 1;
    last_esp_now_activity_ms = millis();
    ap_safety_ap_activated = false;

    sanitize_network_settings();
    apply_button_input_config();
    apply_pcf_input_config();
    if (device_mode == DEVICE_MODE_AP_STA_CLIENT)
    {
        start_soft_ap_runtime();
        connect_wifi_sta_runtime();
    }
    else if (is_ap_enabled_mode(device_mode))
    {
        start_soft_ap_runtime();
    }
    else
    {
        // AP-off mode: optionally start AP at boot for ap_safety_timeout_s (e.g. 5 min) so user can connect
        if (ap_safety_timeout_s > 0 && start_soft_ap_runtime())
        {
            ap_safety_ap_activated = true;
            ap_safety_activated_at_ms = millis();
            Serial.printf("AP safety window started at boot (%us)\n", ap_safety_timeout_s);
        }
        else
        {
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            ap_runtime_enabled = false;
            ap_ip = "";
            esp_err_t channel_result = esp_wifi_set_channel(esp_now_channel, WIFI_SECOND_CHAN_NONE);
            if (channel_result != ESP_OK)
            {
                Serial.printf("Failed to set ESP-NOW channel: %d\n", channel_result);
            }
        }
    }
    digitalWrite(2, 0);
    udp.begin(localPort);
    init_esp_now();
    if (DEBUG)
    {
        Serial.printf("Device mode: %u\n", device_mode);
        Serial.printf("STA SSID: %s\n", sta_ssid.c_str());
        Serial.printf("STA connected: %s\n", sta_connected ? "yes" : "no");
        Serial.printf("STA IP: %s\n", sta_ip.c_str());
        Serial.printf("AP SSID: %s\n", ap_ssid.c_str());
        Serial.printf("AP Password: %s\n", ap_password.c_str());
        Serial.printf("AP IP: %s\n", ap_ip.c_str());
        Serial.printf("ESP-NOW channel: %u\n", esp_now_channel);
        Serial.printf("Mesh TTL: %u\n", mesh_ttl);
        Serial.printf("AP safety timeout: %us\n", ap_safety_timeout_s);
        Serial.printf("UDP port: %u\n", localPort);
        Serial.printf("GPIO14 cfg pull:%u active:%u track:%d\n", button_gpio13_pull_mode, button_gpio13_active_level, button_gpio13_track);
        Serial.printf("GPIO16 cfg pull:%u active:%u track:%d\n", button_gpio16_pull_mode, button_gpio16_active_level, button_gpio16_track);
        Serial.printf("PCF cfg pull:%u active:%u\n", pcf_pull_mode, pcf_active_level);
    }
    server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", String(), false); });
    // server.on(
    //     "/edit", HTTP_POST, [](AsyncWebServerRequest *request)
    //     { request->send(200); },
    //     handleFileUpload);
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request)
              {Serial.printf("Json demandé par le site\n");
              request->send(200, "application/json", local_vars_to_json()); });
    server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "application/json", "{\"ok\":true}"); });
    server.on("/stream/status", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "application/json", stream_status_json()); });
    server.on("/stream/stop", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                  stop_playback();
                  request->send(200, "application/json", stream_status_json());
              });
    server.on(
        "/play", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handlePlay);
    server.on(
        "/stop", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleStop);
    server.on(
        "/delete", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleDelete);
    server.on(
        "/settings", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleSettings);
    server.on(
        "/reorder", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleReorder);
    server.on(
        "/simulate_button", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, handleSimulateButton);
    server.on("/sleep", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                  request_sleep();
                  request->send(200, "application/json", "{\"sleep\":true}");
              });
    server.onFileUpload(handleFileUpload);
    // server.on("/edit", HTTP_POST, handleFileUpload2);

    server.serveStatic("/index.css", SPIFFS, "/index.css");
    server.serveStatic("/index.js", SPIFFS, "/index.js");
    // server.serveStatic("/background.png", SPIFFS, "/background.png");
    // server.serveStatic("/react.svg", SPIFFS, "/react.svg");
    server.serveStatic("/vite.svg", SPIFFS, "/vite.svg");
    server.onNotFound(handleRequest);

    server.begin();
    init_mdns();

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    update_music_from_sd();
    // printf("test : %s\n", files_list[1].c_str());
    if (auto_play)
    {
        play_track_by_index(0);
    }
    if (loop_file)
    {
        audio.setFileLoop(true);
    }

    audio.setVolumeSteps(255); // max 255
    audio.setVolume(volume);
}

bool need_to_play = true;

uint16_t current_Volume = 4095;
char packetBuffer[512]; // Incoming UDP (URLs for stream command)

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
    handle_pending_restart();
    handle_pending_esp_now_commands();
    handle_ap_off_safety_timeout();
    poll_buttons();
    handle_pcf_pending_interrupt();
    update_hardware_power_button();
    update_power_measurements();
    update_leds(millis());
    apply_sleep_if_requested();
    static int32_t test = 0;
    digitalWrite(2, test < 500 ? 0 : 1);
    test++;
    test = test == 1000 ? 0 : test;
    int packetSize = udp.parsePacket();
    if (packetSize)
    {
        int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
        if (len > 0)
        {
            packetBuffer[len] = 0;
        }
        Serial.printf("Data : %s\n", packetBuffer);
        String strData(packetBuffer);
        strData.trim();

        if (strData == "?" || strData.startsWith("?"))
        {
            send_udp_discovery_response();
        }
        else if (strData == "X" || strData.startsWith("X"))
        {
            stop_playback();
        }
        else if (strData.startsWith("S "))
        {
            String url = strData.substring(2);
            url.trim();
            play_stream_by_url(url);
        }
        else
        {
        String data[5]; // Store incoming data

        splitString(strData, ' ', data);
        if (data[0].c_str()[0] == 'V')
        {
            // Volume
            // "V 0" to "V 255"
            Serial.printf("Set Volume to : %ld\n", data[1].toInt());
            audio.setVolume(data[1].toInt());
        }
        else if (data[0].c_str()[0] == 'P')
        {
            // Pause / Resume
            // "P" or "P 0" or "P 1"
            int8_t action = -1;
            if (data[1] != "")
            {
                Serial.printf("Second argument is %ld\n", data[1].toInt());
                action = data[1].toInt();
            }
            if ((action == 0 && audio.isRunning()) || (action == 1 && !audio.isRunning()) || action == -1)
            {
                audio.pauseResume();
                Serial.printf("Music %s\n", audio.isRunning() ? "Resumed" : "Paused");
            }
            else
            {
                Serial.printf("Music allready %s\n", audio.isRunning() ? "Resumed" : "Paused");
            }
        }
        else if (data[0].c_str()[0] == 'L')
        {
            // Loop file
            // "L" or "L 0" or "L 1"
            int8_t action = -1;
            if (data[1] != "")
            {
                Serial.printf("Second argument is %ld\n", data[1].toInt());
                action = data[1].toInt();
            }
            if ((action == -1 && loop_file == false) || action == 1)
            {
                audio.setFileLoop(true);
                loop_file = true;
                Serial.printf("File loop activated\n");
            }
            else if ((action == -1 && loop_file == true) || action == 0)
            {
                audio.setFileLoop(false);
                loop_file = false;
                Serial.printf("File loop deactivated\n");
            }
            else
            {
                Serial.printf("File loop unchanged (%d)\n", loop_file);
            }
        }
        else if (data[0].c_str()[0] == 'B')
        {
            // Balance
            // "B -16" to "B 16"
            //-16 to 16
            audio.setBalance(data[1].toInt());
            Serial.printf("Balance set to %ld\n", data[1].toInt());
        }
        else if (data[0].c_str()[0] == 'J')
        {
            // Jump at position in audio file
            //"J 500" jump in audio file to time
            audio.setAudioPlayPosition(data[1].toInt());
            Serial.printf("Jump in audio file to %ldsecs \n", data[1].toInt());
        }
        // else if (data[0].c_str()[0] == 'J')
        // {
        //     //"J 500" jump in audio file to time
        //     audio.setAudioPlayPosition(data[1].toInt());
        //     Serial.printf("Jump in audio file to %dsecs \n", data[1].toInt());
        // }
        else if (data[0].c_str()[0] == 'T')
        {
            // Set Tonality (more like an equalizer)
            //"T -40 0 6" values can be between -40 ... +6 (dB)
            audio.setTone(data[1].toInt(), data[2].toInt(), data[3].toInt());
            Serial.printf("Tone set to low:%ld band:%ld high:%ld\n", data[1].toInt(), data[2].toInt(), data[3].toInt());
        }
        else if (data[0].c_str()[0] == 'I')
        {
            // Trigger button action through command:
            // "I 13 1" or "I 16 1"
            uint8_t requested_gpio = data[1].toInt();
            int16_t trigger_value = data[2] == "" ? 1 : data[2].toInt();
            if ((requested_gpio == BUTTON_GPIO_13 || requested_gpio == BUTTON_GPIO_16) && trigger_value > 0)
            {
                handle_button_pressed(requested_gpio);
            }
        }
        else
        {
            // Play track
            //"0" to "N" number of tracks in playlist
            uint16_t audio_to_play = data[0].toInt();
            play_track_by_index(audio_to_play);
        }
        }
    }
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
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

#include "PCF8575.h"

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

// LEDs to blink
#define LED_IO12 12
#define LED_IO4 4


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
WiFiUDP udp;
AsyncWebServer server(80);
DNSServer dnsServer;
// Balance control state
int8_t currentBalance = 0;               // valid range: -16 (left) .. +16 (right)
// Persistent current track index
uint16_t currentTrackIndex = 0;
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

// Button long/short press handling
static const uint32_t BUTTON_DEBOUNCE_MS = 120;
static const uint32_t LONG_PRESS_MS = 600;
static const uint32_t REPEAT_FIRST_DELAY_MS = 450;
static const uint32_t REPEAT_RATE_MS = 90;
static const uint8_t VOLUME_STEP = 5;
typedef struct
{
    const uint8_t pin;
    bool isPressed;           // we saw a falling edge and consider it pressed
    bool longFired;           // we already acted on long press
    uint32_t pressStartMs;    // when the press started
    uint32_t nextRepeatMs;    // next time to auto-repeat (inc/dec)
} ButtonState;
static ButtonState btnIncState = { BTN_INC_BAL, false, false, 0, 0 };
static ButtonState btnDecState = { BTN_DEC_BAL, false, false, 0, 0 };
static ButtonState btnPlayPauseState = { BTN_PLAY_PAUSE, false, false, 0, 0 };

void update_spiffs(void);


static void playTrackIndex(uint8_t trackIndex)
{
    if (files_list.size() > trackIndex)
    {
        Serial.printf("[PCF8575] Playing track %u: %s\n", trackIndex, files_list[trackIndex].c_str());
        audio.connecttoFS(SD, files_list[trackIndex].c_str());
        currentTrackIndex = trackIndex;
        if (loop_file)
        {
            audio.setFileLoop(true);
        }
        // Persist the current track for power cycles
        update_spiffs();
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
    const TickType_t debounceTicks = pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS);
    for (;;)
    {
        bool got = xQueueReceive(buttonQueue, &evt, pdMS_TO_TICKS(20)) == pdTRUE;
        if (got)
        {
            TickType_t now = xTaskGetTickCount();
            if (evt != BTN_EVT_PCF_INT && (now - lastChange < debounceTicks))
                continue;
            lastChange = now;

            if (evt == BTN_EVT_INC)
            {
                // Press start for INC
                if (!btnIncState.isPressed && digitalRead(btnIncState.pin) == LOW)
                {
                    btnIncState.isPressed = true;
                    btnIncState.longFired = false;
                    btnIncState.pressStartMs = millis();
                    btnIncState.nextRepeatMs = btnIncState.pressStartMs + REPEAT_FIRST_DELAY_MS;
                }
            }
            else if (evt == BTN_EVT_DEC)
            {
                // Press start for DEC
                if (!btnDecState.isPressed && digitalRead(btnDecState.pin) == LOW)
                {
                    btnDecState.isPressed = true;
                    btnDecState.longFired = false;
                    btnDecState.pressStartMs = millis();
                    btnDecState.nextRepeatMs = btnDecState.pressStartMs + REPEAT_FIRST_DELAY_MS;
                }
            }
            else if (evt == BTN_EVT_PLAY_PAUSE)
            {
                // Press start for PLAY/PAUSE
                if (!btnPlayPauseState.isPressed && digitalRead(btnPlayPauseState.pin) == LOW)
                {
                    btnPlayPauseState.isPressed = true;
                    btnPlayPauseState.longFired = false;
                    btnPlayPauseState.pressStartMs = millis();
                }
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
                    // On any button press on the IO expander, play the current track
                    if (pcfPlayQueue)
                    {
                        uint8_t idx = 0;
                        if (!files_list.empty())
                        {
                            idx = (currentTrackIndex < files_list.size()) ? (uint8_t)currentTrackIndex : 0;
                        }
                        xQueueSend(pcfPlayQueue, &idx, 0);
                    }
                }
                pcfPrevValue = pcfValue;
            }
        }
        // Polling section to classify long/short and handle auto-repeat
        uint32_t nowMs = millis();
        // INC button
        if (btnIncState.isPressed)
        {
            if (digitalRead(btnIncState.pin) == LOW)
            {
                if (!btnIncState.longFired && (nowMs - btnIncState.pressStartMs >= LONG_PRESS_MS))
                {
                    // Long press action: next track (single action)
                    if (!files_list.empty())
                    {
                        uint8_t next = files_list.size() > 0 ? (uint8_t)((currentTrackIndex + 1) % files_list.size()) : 0;
                        playTrackIndex(next);
                        Serial.printf("[BTN IO14] Next track -> %u (long press)\n", next);
                    }
                    btnIncState.longFired = true;
                }
            }
            else
            {
                // Released
                if (!btnIncState.longFired)
                {
                    int newVol = volume + VOLUME_STEP;
                    if (newVol > 255) newVol = 255;
                    if ((uint8_t)newVol != volume)
                    {
                        volume = (uint8_t)newVol;
                        audio.setVolume(volume);
                        update_spiffs();
                    }
                    Serial.printf("[BTN IO14] Volume: %u (short)\n", (unsigned)volume);
                }
                btnIncState.isPressed = false;
            }
        }
        // DEC button
        if (btnDecState.isPressed)
        {
            if (digitalRead(btnDecState.pin) == LOW)
            {
                if (!btnDecState.longFired && (nowMs - btnDecState.pressStartMs >= LONG_PRESS_MS))
                {
                    // Long press action: previous track (single action)
                    if (!files_list.empty())
                    {
                        uint8_t size = (uint8_t)files_list.size();
                        uint8_t prev = size > 0 ? (uint8_t)((currentTrackIndex + size - 1) % size) : 0;
                        playTrackIndex(prev);
                        Serial.printf("[BTN IO16] Previous track -> %u (long press)\n", prev);
                    }
                    btnDecState.longFired = true;
                }
            }
            else
            {
                // Released
                if (!btnDecState.longFired)
                {
                    int newVol = (int)volume - (int)VOLUME_STEP;
                    if (newVol < 0) newVol = 0;
                    if ((uint8_t)newVol != volume)
                    {
                        volume = (uint8_t)newVol;
                        audio.setVolume(volume);
                        update_spiffs();
                    }
                    Serial.printf("[BTN IO16] Volume: %u (short)\n", (unsigned)volume);
                }
                btnDecState.isPressed = false;
            }
        }
        // PLAY/PAUSE button
        if (btnPlayPauseState.isPressed)
        {
            if (digitalRead(btnPlayPauseState.pin) == LOW)
            {
                if (!btnPlayPauseState.longFired && (nowMs - btnPlayPauseState.pressStartMs >= LONG_PRESS_MS))
                {
                    // Long press action: start playing current track
                    if (!files_list.empty())
                    {
                        uint8_t idx = (files_list.size() > 0 && currentTrackIndex < files_list.size()) ? (uint8_t)currentTrackIndex : 0;
                        playTrackIndex(idx);
                        Serial.printf("[BTN IO33] Play track -> %u (long press)\n", idx);
                    }
                    else
                    {
                        Serial.println("[BTN IO33] No tracks to play (long)");
                    }
                    btnPlayPauseState.longFired = true;
                }
            }
            else
            {
                // Released
                if (!btnPlayPauseState.longFired)
                {
                    // Short press action: toggle pause/resume
                    audio.pauseResume();
                    Serial.printf("[BTN IO33] %s (short)\n", audio.isRunning() ? "Resumed" : "Paused");
                }
                btnPlayPauseState.isPressed = false;
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
    doc["current_track"] = currentTrackIndex;
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
    if (doc.containsKey("current_track"))
        currentTrackIndex = doc["current_track"].as<unsigned int>();
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
    Serial.printf("SPIFFS current_track : %u\n", currentTrackIndex);
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
    // Persist selected track
    currentTrackIndex = file_index;
    update_spiffs();
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
    ledcSetup(0, 12000, 16);
    ledcSetup(1, 12000, 16);
    // Assigne le canal PWM au pins
    ledcAttachPin(13, 0);

    pinMode(SD_CS, OUTPUT);
    pinMode(2, OUTPUT); ///
    pinMode(I2S_ENABLE, OUTPUT);
    digitalWrite(I2S_ENABLE, 1);
    pinMode(BTN_INC_BAL, INPUT_PULLUP);
    pinMode(BTN_DEC_BAL, INPUT_PULLUP);
    pinMode(BTN_PLAY_PAUSE, INPUT_PULLUP);
    pinMode(LED_IO12, OUTPUT);
    pinMode(LED_IO4, OUTPUT);
    digitalWrite(LED_IO12, LOW);
    digitalWrite(LED_IO4, LOW);
    // Setup FreeRTOS queue and task for button events
    buttonQueue = xQueueCreate(8, sizeof(ButtonEvent));
    // Increase stack to withstand queue ops and PCF ISR handling
    xTaskCreatePinnedToCore(buttonTask, "buttonTask", 4096, NULL, 2, &buttonTaskHandle, 1);
    // Attach interrupts on falling edge (active low buttons)
    attachInterrupt(digitalPinToInterrupt(BTN_INC_BAL), isrBtnInc, FALLING);
    attachInterrupt(digitalPinToInterrupt(BTN_DEC_BAL), isrBtnDec, FALLING);
    attachInterrupt(digitalPinToInterrupt(BTN_PLAY_PAUSE), isrBtnPlayPause, FALLING);
    // PCF8575 interrupt line on GPIO15
    pinMode(PCF_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PCF_INT_PIN), isrPCFInt, FALLING);
    digitalWrite(2, 1); ///
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

    // Queue for deferred audio playback from PCF events
    pcfPlayQueue = xQueueCreate(8, sizeof(uint8_t));

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
    // server.on(
    //     "/edit", HTTP_POST, [](AsyncWebServerRequest *request)
    //     { request->send(200); },
    //     handleFileUpload);
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

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    update_music_from_sd();
    // printf("test : %s\n", files_list[1].c_str());
    if (auto_play)
    {
        uint16_t idx = (files_list.size() > 0 && currentTrackIndex < files_list.size()) ? currentTrackIndex : 0;
        playTrackIndex(idx);
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
    dnsServer.processNextRequest();
    // Blink LEDs on IO12 and IO4 (toggle every 500 ms)
    static uint32_t lastBlinkMs = 0;
    static bool blinkState = false;
    uint32_t nowMs = millis();
    if (nowMs - lastBlinkMs >= 100)
    {
        lastBlinkMs = nowMs;
        blinkState = !blinkState;
        digitalWrite(LED_IO12, blinkState ? LOW : HIGH);
        digitalWrite(LED_IO4, blinkState ? HIGH : LOW);
    }
    // PCF8575 interrupt processing moved to FreeRTOS task (pcfIntTask)
    static int32_t test = 0;
    digitalWrite(2, test < 500 ? 0 : 1);
    test++;
    test = test == 1000 ? 0 : test;
    // Button handling moved to ISR + FreeRTOS task
    // Handle deferred playback requests from PCF task
    uint8_t playIdx;
    while (pcfPlayQueue && xQueueReceive(pcfPlayQueue, &playIdx, 0) == pdTRUE)
    {
        playTrackIndex(playIdx);
    }
    int packetSize = udp.parsePacket();
    if (packetSize)
    {
        // Read the packet into packetBuffer
        int len = udp.read(packetBuffer, 255);
        if (len > 0)
        {
            packetBuffer[len] = 0;
        }
        Serial.printf("Data : %s\n", packetBuffer);
        String strData(packetBuffer);
        String data[5]; // Store incoming data

        splitString(strData, ' ', data);
        if (data[0].c_str()[0] == 'V')
        {
            // Volume
            // "V 0" to "V 255"
            Serial.printf("Set Volume to : %d\n", data[1].toInt());
            audio.setVolume(data[1].toInt());
        }
        else if (data[0].c_str()[0] == 'P')
        {
            // Pause / Resume
            // "P" or "P 0" or "P 1"
            int8_t action = -1;
            if (data[1] != "")
            {
                Serial.printf("Second argument is %d\n", data[1].toInt());
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
                Serial.printf("Second argument is %d\n", data[1].toInt());
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
            Serial.printf("Balance set to \n", data[1].toInt());
        }
        else if (data[0].c_str()[0] == 'J')
        {
            // Jump at position in audio file
            //"J 500" jump in audio file to time
            audio.setAudioPlayPosition(data[1].toInt());
            Serial.printf("Jump in audio file to %dsecs \n", data[1].toInt());
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
            Serial.printf("Tone set to low:%d band:%d high:%d\n", data[1].toInt(), data[2].toInt(), data[3].toInt());
        }
        else if (data[0].c_str()[0] == 'I')
        {
            // Set GPIO to value
            if (data[1].toInt() == 13)
            {
                ledcWrite(0, data[2].toInt());
                Serial.printf("GPIO 13 set to :%d\n", data[2].toInt());
            }
            else if (data[1].toInt() == 16)
            {
                // IO16 is used as a button input; ignore PWM writes
                Serial.printf("GPIO 16 is reserved for button input, ignoring write\n");
            }
        }
        else
        {
            // Play track
            //"0" to "N" number of tracks in playlist
            uint16_t audio_to_play = data[0].toInt();
            if (files_list.size() > audio_to_play)
            {
                printf("Playing : %s\n", files_list[audio_to_play].c_str());
                audio.connecttoFS(SD, files_list[audio_to_play].c_str());
                currentTrackIndex = audio_to_play;
                update_spiffs();
            }
            else
            {
                printf("Sound number %d is out of range\n", audio_to_play);
            }
            if (loop_file)
            {
                audio.setFileLoop(true);
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
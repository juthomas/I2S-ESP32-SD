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
#include "Audio.h"
#include "esp_now.h"
#include "SD.h"
#include "FS.h"
#include "sstream"
#include <algorithm>

// branchement Carte SD
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

// Branchement Amplificateur I2S
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC 26

unsigned int localPort = 8266;     // port de reception UDP

bool loop_file = true;               // Default loop audio files
bool auto_play = false;              // Lit la premiere track au demarrage
const bool DEBUG = true;             // Afficher les messages dans la console

const uint8_t BUTTON_GPIO_13 = 13;
const uint8_t BUTTON_GPIO_16 = 16;
const uint16_t BUTTON_DEBOUNCE_MS = 40;
const uint8_t ESP_NOW_BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t ESP_NOW_PACKET_MAGIC = 0xA5;
const uint8_t ESP_NOW_PACKET_VERSION = 0x01;
const uint8_t ESP_NOW_CMD_PLAY_TRACK = 0x01;
const size_t SETTINGS_DOC_CAPACITY = 4096;

String ap_name = "I2S-SD";
String ap_ssid = "";
String ap_password = "12345678";
String ap_ip = "";
String ap_ip_config = "192.168.4.1";
uint8_t esp_now_channel = 6;
int16_t button_gpio13_track = 0;
int16_t button_gpio16_track = 1;
bool esp_now_ready = false;
volatile bool restart_requested = false;
uint32_t restart_requested_at_ms = 0;

typedef struct __attribute__((packed)) s_esp_now_packet
{
    uint8_t magic;
    uint8_t version;
    uint8_t cmd;
    uint8_t reserved;
    uint16_t track_index;
} t_esp_now_packet;

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

volatile bool esp_now_pending_track = false;
volatile uint16_t esp_now_track_to_play = 0;
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
Audio audio;
WiFiUDP udp;
AsyncWebServer server(80);

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
    DynamicJsonDocument doc(SETTINGS_DOC_CAPACITY);

    doc["loop_file"] = loop_file;
    doc["auto_play"] = auto_play;
    doc["note"] = note;
    doc["udp_port"] = localPort;
    doc["volume"] = volume;
    doc["ap_ssid"] = ap_ssid;
    doc["ap_ip"] = ap_ip;
    doc["ap_name"] = ap_name;
    doc["ap_password"] = ap_password;
    doc["ap_ip_config"] = ap_ip_config;
    doc["esp_now_channel"] = esp_now_channel;
    doc["button_gpio13_track"] = button_gpio13_track;
    doc["button_gpio16_track"] = button_gpio16_track;
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
    if (doc.containsKey("button_gpio16_track"))
    {
        int16_t tmp_track = doc["button_gpio16_track"].as<int>();
        button_gpio16_track = tmp_track < -1 ? -1 : tmp_track;
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
    Serial.printf("SPIFFS note : %s\n", note.c_str());
    Serial.printf("SPIFFS udp_port : %d\n", localPort);
    Serial.printf("SPIFFS volume : %d\n", volume);
    Serial.printf("SPIFFS ap_ssid : %s\n", ap_ssid.c_str());
    Serial.printf("SPIFFS ap_name : %s\n", ap_name.c_str());
    Serial.printf("SPIFFS ap_ip_config : %s\n", ap_ip_config.c_str());
    Serial.printf("SPIFFS esp_now_channel : %u\n", esp_now_channel);
    Serial.printf("SPIFFS button_gpio13_track : %d\n", button_gpio13_track);
    Serial.printf("SPIFFS button_gpio16_track : %d\n", button_gpio16_track);

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
    if (doc.containsKey("button_gpio13_track"))
    {
        button_gpio13_track = doc["button_gpio13_track"].as<int>();
        Serial.print("button_gpio13_track on sd card :");
        Serial.println(button_gpio13_track);
    }
    if (doc.containsKey("button_gpio16_track"))
    {
        button_gpio16_track = doc["button_gpio16_track"].as<int>();
        Serial.print("button_gpio16_track on sd card :");
        Serial.println(button_gpio16_track);
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

String get_device_suffix()
{
    uint64_t chip_id = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06lX", (unsigned long)(chip_id & 0xFFFFFF));
    return String(suffix);
}

String build_ap_ssid()
{
    String prefix = ap_name;
    prefix.trim();
    if (prefix.length() == 0)
        prefix = "I2S-SD";
    if (prefix.length() > 24)
        prefix = prefix.substring(0, 24);
    return prefix + "-" + get_device_suffix();
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
        ap_name = "I2S-SD";
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

    IPAddress parsed_ip;
    if (!parse_ipv4_string(ap_ip_config, parsed_ip))
    {
        Serial.println("Invalid AP IP, fallback to 192.168.4.1");
        ap_ip_config = "192.168.4.1";
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

bool play_track_by_index(uint16_t audio_to_play)
{
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
        if (loop_file)
        {
            audio.setFileLoop(true);
        }
        return true;
    }
    Serial.printf("Sound number %d is out of range\n", audio_to_play);
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

void on_esp_now_receive(const uint8_t *mac_addr, const uint8_t *incoming_data, int len)
{
    (void)mac_addr;
    if (len != sizeof(t_esp_now_packet))
        return;

    t_esp_now_packet packet;
    memcpy(&packet, incoming_data, sizeof(packet));
    if (packet.magic != ESP_NOW_PACKET_MAGIC || packet.version != ESP_NOW_PACKET_VERSION || packet.cmd != ESP_NOW_CMD_PLAY_TRACK)
        return;

    portENTER_CRITICAL_ISR(&esp_now_mux);
    esp_now_track_to_play = packet.track_index;
    esp_now_pending_track = true;
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
    esp_now_ready = true;
    Serial.println("ESP-NOW initialized");
    return true;
}

void send_esp_now_play_track(uint16_t track_index)
{
    if (!esp_now_ready)
        return;

    t_esp_now_packet packet = {
        .magic = ESP_NOW_PACKET_MAGIC,
        .version = ESP_NOW_PACKET_VERSION,
        .cmd = ESP_NOW_CMD_PLAY_TRACK,
        .reserved = 0,
        .track_index = track_index};

    esp_err_t send_result = esp_now_send(ESP_NOW_BROADCAST_ADDR, (uint8_t *)&packet, sizeof(packet));
    if (send_result != ESP_OK)
    {
        Serial.printf("ESP-NOW send failed: %d\n", send_result);
    }
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
        if (button.stable_state == LOW)
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
    if (!esp_now_pending_track)
        return;

    uint16_t track_to_play = 0;
    portENTER_CRITICAL(&esp_now_mux);
    track_to_play = esp_now_track_to_play;
    esp_now_pending_track = false;
    portEXIT_CRITICAL(&esp_now_mux);

    Serial.printf("ESP-NOW track received: %u\n", track_to_play);
    play_track_by_index(track_to_play);
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
    uint8_t previous_channel = esp_now_channel;
    unsigned int previous_udp_port = localPort;

    Serial.printf("Handle settings body size: %u\n", len);
    json_to_local_vars(data, len);
    sanitize_network_settings();
    update_spiffs();

    bool network_changed = previous_ap_ssid != ap_ssid ||
                           previous_ap_name != ap_name ||
                           previous_ap_password != ap_password ||
                           previous_ap_ip_config != ap_ip_config ||
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

void setup()
{
    pinMode(BUTTON_GPIO_13, INPUT_PULLUP);
    pinMode(BUTTON_GPIO_16, INPUT_PULLUP);
    button_states[0].stable_state = digitalRead(BUTTON_GPIO_13);
    button_states[0].last_reading = button_states[0].stable_state;
    button_states[1].stable_state = digitalRead(BUTTON_GPIO_16);
    button_states[1].last_reading = button_states[1].stable_state;

    pinMode(SD_CS, OUTPUT);
    pinMode(2, OUTPUT); ///
    digitalWrite(2, 1); ///
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    Serial.begin(115200);
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

    sanitize_network_settings();
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    IPAddress configured_ap_ip;
    parse_ipv4_string(ap_ip_config, configured_ap_ip);
    IPAddress ap_subnet(255, 255, 255, 0);
    if (!WiFi.softAPConfig(configured_ap_ip, configured_ap_ip, ap_subnet))
    {
        Serial.println("AP IP config failed, continuing with defaults");
    }
    ap_ssid = resolve_ap_ssid();
    bool ap_started = WiFi.softAP(ap_ssid.c_str(), ap_password.c_str(), esp_now_channel, false, 4);
    if (!ap_started)
    {
        Serial.println("AP initialization failed!");
    }
    ap_ip = WiFi.softAPIP().toString();
    ap_ip_config = ap_ip;
    digitalWrite(2, 0);
    udp.begin(localPort);
    init_esp_now();
    if (DEBUG)
    {
        Serial.printf("AP SSID: %s\n", ap_ssid.c_str());
        Serial.printf("AP IP: %s\n", ap_ip.c_str());
        Serial.printf("ESP-NOW channel: %u\n", esp_now_channel);
        Serial.printf("UDP port: %u\n", localPort);
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
    handle_pending_restart();
    handle_pending_esp_now_commands();
    poll_buttons();
    static int32_t test = 0;
    digitalWrite(2, test < 500 ? 0 : 1);
    test++;
    test = test == 1000 ? 0 : test;
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
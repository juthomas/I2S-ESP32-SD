#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"

// Digital I/O used
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18
#define I2S_DOUT 25 //
#define I2S_BCLK 27 //
#define I2S_LRC 26  //

Audio audio;

String ssid = "SFR_B4C8";
String password = "enorksenez3vesterish";

IPAddress ip(192, 168, 0, 225);      // Local IP (static)
IPAddress gateway(192, 168, 0, 1);   // Router IP
const unsigned int localPort = 8266; // port
IPAddress subnet(255, 255, 255, 0);
WiFiUDP udp;

const boolean DEBUG = true; // baud rate : 115200
std::vector<std::string> files_list;

std::vector<std::string> listSdFiles(const char *dirname)
{
    std::vector<std::string> fileList;

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
        if (!nameString.startsWith("/.") && (nameString.endsWith(".wav") || nameString.endsWith(".mp3") ))
        {
            // Afficher le nom du fichier
            Serial.println(entry.name());
            fileList.push_back(std::string(entry.name()));
        }
        // Fermer le fichier
        entry.close();
    }

    // Fermer le répertoire
    root.close();
    return fileList;
}

void setup()
{
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    Serial.begin(115200);
    // SD.begin(SD_CS);
    if (!SD.begin(SD_CS))
    {
        Serial.println("initialization failed!");
        while (1)
            ;
    }
    Serial.println("initialization done.");

    // WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    // WiFi.config(ip, gateway, subnet); // Static IP Address
    WiFi.begin(ssid.c_str(), password.c_str());

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
    }
    udp.begin(localPort);

    if (DEBUG)
    {
        Serial.begin(115200);
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    }

    // while (WiFi.status() != WL_CONNECTED) delay(1500);
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    // audio.setVolumeSteps(4095);
    audio.setVolume(5); // default 0...21
    files_list = listSdFiles("/");

    printf("test : %s\n", files_list[1].c_str());
    audio.connecttoFS(SD, files_list[1].c_str());

    //  or alternative
     audio.setVolumeSteps(255); // max 255
     audio.setVolume(63);
    //
    //  *** radio streams ***
    // audio.connecttohost("http://stream.antennethueringen.de/live/aac-64/stream.antennethueringen.de/"); // aac
    //  audio.connecttohost("http://mcrscast.mcr.iol.pt/cidadefm");                                         // mp3
    //  audio.connecttohost("http://www.wdr.de/wdrlive/media/einslive.m3u");                                // m3u
    //  audio.connecttohost("https://stream.srg-ssr.ch/rsp/aacp_48.asx");                                   // asx
    //  audio.connecttohost("http://tuner.classical102.com/listen.pls");                                    // pls
    //  audio.connecttohost("http://stream.radioparadise.com/flac");                                        // flac
    //  audio.connecttohost("http://stream.sing-sing-bis.org:8000/singsingFlac");                           // flac (ogg)
    //  audio.connecttohost("http://s1.knixx.fm:5347/dein_webradio_vbr.opus");                              // opus (ogg)
    //  audio.connecttohost("http://stream2.dancewave.online:8080/dance.ogg");                              // vorbis (ogg)
    //  audio.connecttohost("http://26373.live.streamtheworld.com:3690/XHQQ_FMAAC/HLSTS/playlist.m3u8");    // HLS
    //  audio.connecttohost("http://eldoradolive02.akamaized.net/hls/live/2043453/eldorado/master.m3u8");   // HLS (ts)
    //  *** web files ***
    //  audio.connecttohost("https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/Pink-Panther.wav");        // wav
    //  audio.connecttohost("https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/Santiano-Wellerman.flac"); // flac
    //  audio.connecttohost("https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/Olsen-Banden.mp3");        // mp3
    //  audio.connecttohost("https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/Miss-Marple.m4a");         // m4a (aac)
    //  audio.connecttohost("https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/Collide.ogg");             // vorbis
    //  audio.connecttohost("https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/sample.opus");             // opus
    //  *** local files ***
    //  audio.connecttoFS(SD, "/audio.wav");     // SD
    // audio.connecttoFS(SD, "/Little London Girl(lyrics).mp3");
    // audio.connecttoFS(SD, "/Synthwave.mp3");
    // audio.connecttoFS(SD, "/lofi.mp3");
    // audio.connecttoFS(SD, "/HATEFUL LOVE.mp3"); z
    //  audio.connecttoFS(SD_MMC, "/test.wav"); // SD_MMC
    //  audio.connecttoFS(SPIFFS, "/test.wav"); // SPIFFS

    //  audio.connecttospeech("Wenn die Hunde schlafen, kann der Wolf gut Schafe stehlen.", "de"); // Google TTS
}

bool need_to_play = true;

uint16_t current_Volume = 4095;
char packetBuffer[255]; // Incoming

// Méthode pour découper le message avec un séparateur (ou "parser")
void splitString(String message, char separator, String data[2])
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
        String data[2]; // Store incoming data

        splitString(strData, ' ', data);
        if (data[0].c_str()[0] == 'V')
        {
            Serial.printf("Set Volume to : %d\n", data[1].toInt());
             audio.setVolume(data[1].toInt());
        }
        else if (data[0].c_str()[0] == 'P')
        {
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
            
            audio.setFileLoop(true);
            audio.
            // int8_t action = -1;
            // if (data[1] != "")
            // {
            //     Serial.printf("Second argument is %d\n", data[1].toInt());
            //     action = data[1].toInt();
            // }
            // if ((action == 0 && audio.isRunning()) || (action == 1 && !audio.isRunning()) || action == -1)
            // {
            //     audio.pauseResume();
            //     Serial.printf("Music %s\n", audio.isRunning() ? "Resumed" : "Paused");
            // }
            // else
            // {
            //     Serial.printf("Music allready %s\n", audio.isRunning() ? "Resumed" : "Paused");
            // }
        }
        else
        {
            uint16_t audio_to_play = data[0].toInt();
            if (files_list.size() > audio_to_play)
            {
                printf("Playing : %s\n", files_list[audio_to_play].c_str());
                audio.connecttoFS(SD, files_list[audio_to_play].c_str());
            }
            else
            {
                printf("Sound number %d is out of range\n", audio_to_play);
            }
        }
        // analogWrite(data[0].toInt(), data[1].toInt());
    }
    // static bool low_volume = false;
    // if (need_to_play)
    // {
    //     need_to_play = false;
    //     // audio.connecttoFS(SD, "/Synthwave.mp3");
    //     // audio.connecttoFS(SD, "/lofi.mp3");
    //     // audio.connecttoFS(SD, "/HATEFUL LOVE.mp3");
    //     // audio.connecttoFS(SD, "/flute-s1.aif");
    //     // audio.connecttoFS(SD, "/cantos.wav");
    //     // audio.connecttoFS(SD, "/IKE_CHANT_AUX_CHIENS.wav");
    //     // audio.connecttoFS(SD, "/inuit.wav");
    //     audio.connecttoFS(SD, "/fab-vinz.wav");
    //     // audio.setVolume(low_volume ? 10 : 21); // default 0...21
    //     low_volume = !low_volume;
    // }
    // uint16_t potentiometer = analogRead(34);
    // if (potentiometer <= current_Volume - 100 || potentiometer >= current_Volume + 100)
    // {
    //     current_Volume =  potentiometer;
    //     audio.setVolume(current_Volume);
    //     Serial.printf("Volume changed to : %d\n", current_Volume);
    // }
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
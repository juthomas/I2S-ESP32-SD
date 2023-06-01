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
#include "ESPAsyncWebServer.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"

// branchement Carte SD
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

// Branchement Amplificateur I2S
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC 26

String ssid = "SFR_B4C8";                 // nom du routeur
String password = "enorksenez3vesterish"; // mot de passe

IPAddress ip(192, 168, 0, 225);      // Local IP (static)
IPAddress gateway(192, 168, 0, 1);   // Router IP
const unsigned int localPort = 8266; // port de reception UDP
IPAddress subnet(255, 255, 255, 0);

bool loop_file = true;                // Default loop audio files
const bool REQUEST_STATIC_IP = false; // Demander l'attribution d'une ip statique
const bool AUTO_PLAY_TRACK = false;   // Lit la premiere track au demarrage
const bool DEBUG = true;              // Afficher les messages dans la console
std::vector<std::string> files_list;
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
        
        request->send(200);
    }
}

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
        if (!nameString.startsWith("/.") && (nameString.endsWith(".wav") || nameString.endsWith(".mp3")))
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
    WiFi.mode(WIFI_STA);
    if (REQUEST_STATIC_IP)
    {
        WiFi.config(ip, gateway, subnet); // Static IP Address
    }
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
    server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", String(), false); });
    // server.on(
    //     "/edit", HTTP_POST, [](AsyncWebServerRequest *request)
    //     { request->send(200); },
    //     handleFileUpload);
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
    files_list = listSdFiles("/");

    // printf("test : %s\n", files_list[1].c_str());
    if (AUTO_PLAY_TRACK)
    {
        audio.connecttoFS(SD, files_list[0].c_str());
    }

    audio.setVolumeSteps(255); // max 255
    audio.setVolume(63);
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
        else
        {
            // Play track
            //"0" to "N" number of tracks in playlist
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
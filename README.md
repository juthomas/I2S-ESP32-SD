# Lecteur Wav/Mp3/m4a Avec des signaux UDP
Ce projet est un lecteur audio qui permet de lire des fichiers audio au format WAV, MP3 et M4A à partir d'une carte SD. Il utilise des signaux UDP pour contrôler la lecture audio à distance.

## Dépendances
Ce projet dépend de la bibliothèque ESP32-audioI2S, disponible à l'adresse suivante : https://github.com/schreibfaul1/ESP32-audioI2S

## Configuration requise
La carte SD doit être formatée en FAT16 ou FAT32. Pour le format FAT32, il est recommandé d'utiliser l'outil de formatage disponible à l'adresse : https://www.sdcard.org/downloads/formatter/
Les fichiers audio doivent être placés à la racine de la carte SD.

## Signaux UDP pris en charge
Le lecteur audio peut être contrôlé à l'aide des signaux UDP suivants :

- **Track** : `0` à `nombre de tracks audios` : permet de sélectionner la piste audio à lire.
- **Volume** : `V 0` à `V 255` : permet de régler le volume de lecture.
- **Pause/Reprise** : `P` : alterne entre la pause et la reprise de la lecture. `P 0` met en pause la lecture et `P 1` reprend la lecture.
- **Loop file** : `L` : alterne entre le mode de lecture en boucle et le mode de lecture unique. `L 0` désactive la lecture en boucle et `L 1` active la lecture en boucle.
- **Balance** : `B -16` à `B 16` : permet de régler l'équilibre audio entre les canaux gauche et droit.
- **Jump** : `J 60` : permet de sauter à un endroit spécifique dans la piste audio. Le nombre spécifié représente le nombre de secondes.
- **Tonality** : `T -40 0 6` : fonctionne comme un égaliseur, où le premier nombre correspond au gain pour les graves, le deuxième pour les médiums et le troisième pour les aigus. Le gain peut varier de -40 à 6 (en dB).
- **GPIO** : `I 13 65535` : controlle un GPIO de la carte (13 ou 16) avec un niveau d'intensitée allant de 0 à 65535 (de 0V à 3.3V).


Les signaux UDP peuvent être testés et envoyés grâve à la commande `nc -u 192.168.1.96 8266` où l'ip et le port de l'esp32 sont à renseigner
## Branchements
- Carte SD :
	- **CS** : Broche 5
	- **MOSI** : Broche 23
	- **MISO** : Broche 19
	- **SCK** : Broche 18
- Amplificateur I2S :
	- **DIN** : Broche 25
	- **BCLK** : Broche 27
	- **LRC** : Broche 26

## Configuration réseau

### Configuration Wi-Fi
Vous devez spécifier les détails de votre réseau Wi-Fi :

```cpp
String ssid =  "NomDuRouteur"; // Nom du réseau Wi-Fi
String password = "MotDePasse";   // Mot de passe du réseau Wi-Fi
```
Vous pouvez décommenter l'une des lignes ssid et pass en fonction de votre réseau, ou les modifier pour correspondre à votre réseau personnel.

### Configuration de l'adresse IP
Si vous souhaitez utiliser une adresse IP statique pour l'ESP32, vous pouvez la configurer en modifiant les lignes suivantes :

```cpp
IPAddress ip(192, 168, 0, 215);    // Adresse IP locale (statique)
IPAddress gateway(192, 168, 0, 1); // Adresse IP du routeur
const unsigned int localPort = 8266; // Port de réception UDP
IPAddress subnet(255, 255, 255, 0); // Masque de sous-réseau
```
Assurez-vous de spécifier les bonnes adresses IP pour votre réseau.

## Variables de configuration
- **loop_file** : Booléen (true/false) : indique si la lecture des fichiers audio doit être en boucle par défaut.
- **REQUEST_STATIC_IP** : Booléen (true/false) : demande l'attribution d'une adresse IP statique.
- **AUTO_PLAY_TRACK** : Booléen (true/false) : lit automatiquement la première piste audio au démarrage.
- **DEBUG** : Booléen (true/false) : affiche les messages de débogage dans la console.

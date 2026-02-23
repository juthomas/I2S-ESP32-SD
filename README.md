# Lecteur Wav/Mp3/m4a (ESP-NOW + AP)

Ce projet lit des fichiers audio WAV/MP3/M4A depuis la carte SD avec un ESP32.
Chaque device démarre maintenant en:

- **Mode AP** (SSID unique par device) pour accéder a l'interface web de configuration a tout moment.
- **Mode ESP-NOW** pour diffuser des ordres de lecture a tous les devices sans routeur.
- **UDP local** (port configurable) conserve pour pilotage direct sur le reseau AP du device.

## Dependances

Le projet depend de `ESP32-audioI2S`:
https://github.com/schreibfaul1/ESP32-audioI2S

## Configuration requise

- Carte SD formatee en FAT16/FAT32.
- Fichiers audio places a la racine de la carte SD.

## Interface web et configuration

L'interface web permet de configurer:

- `loop_file`
- `auto_play`
- `note`
- `udp_port`
- `volume`
- `button_gpio13_track`
- `button_gpio16_track`

Les champs boutons utilisent l'index de piste:

- `0` = premiere piste
- `1` = deuxieme piste
- `-1` = desactive le bouton

Quand un bouton physique (`GPIO 13` ou `GPIO 16`) est presse, le device joue la piste configuree et diffuse la commande a tous les autres devices via ESP-NOW.

## Signaux UDP pris en charge

- **Track** : `0` a `nombre de pistes`.
- **Volume** : `V 0` a `V 255`.
- **Pause/Reprise** : `P`, `P 0`, `P 1`.
- **Loop** : `L`, `L 0`, `L 1`.
- **Balance** : `B -16` a `B 16`.
- **Jump** : `J 60`.
- **Tonality** : `T -40 0 6`.
- **Declenchement bouton** : `I 13 1` ou `I 16 1` (simule un appui bouton et diffuse la piste configuree).

Exemple test UDP:
`nc -u <ip_ap_esp32> 8266`

## Branchements

- Carte SD
  - **CS**: GPIO 5
  - **MOSI**: GPIO 23
  - **MISO**: GPIO 19
  - **SCK**: GPIO 18
- Amplificateur I2S
  - **DIN**: GPIO 25
  - **BCLK**: GPIO 27
  - **LRC**: GPIO 26
- Boutons
  - **BTN1**: GPIO 13 (INPUT_PULLUP)
  - **BTN2**: GPIO 16 (INPUT_PULLUP)

## Configuration optionnelle via `/config.json` sur SD

Exemple:

```json
{
  "ap_name": "I2S-SD",
  "ap_password": "12345678",
  "esp_now_channel": 6,
  "button_gpio13_track": 0,
  "button_gpio16_track": 1
}
```

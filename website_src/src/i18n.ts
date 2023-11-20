import i18n from "i18next";
import { initReactI18next } from "react-i18next";

const getCurrentLang = (): string => navigator.language.split(/-|_/)[0];

i18n.use(initReactI18next).init({
  fallbackLng: "en",
  lng: getCurrentLang(),
  interpolation: {
    escapeValue: false,
  },
  resources: {
    en: {
      translation: {
        Parameters: {
          parameters: "Parameters",
          loopAudio: "Audio loop",
          autoPlay: "Auto play",
          notes: "Notes",
          udpPort: "UDP Port",
          volume: "volume",
          save: "Save",
        },
        UploadFile: {
          hoursLeft: "{{hours}} hours left",
          minsLeft: "{{mins}} mins left",
          secsLeft: "{{secs}} secs left",
          dropFile: "Drop your files here",
          orSelectFile: "or clic on select file",
          selectFile: "Select file",
          fileToImport: "File to import",
          size: "Size",
          downloadOnComputer: "Download on computer",
          download: "Download",
          sendFile: "Send file",
        },
        AudioList: {
          filesOnSdCard: "Files on SD card",
          download: "Download",
          play: "Play",
          stop: "Stop",
          suppress: "Suppress",
          file: "File",
          index: "Index",
          downloadOnComputer: "Download on computer",
          playOnESP: "Play on ESP",
        },
        title: "Multi-language app",
        label: "Select another language!",
        about: "About",
        home: "Home",
      },
    },
    fr: {
      translation: {
        Parameters: {
          parameters: "Paramètres",
          loopAudio: "Boucle audio",
          autoPlay: "Lecture automatique",
          notes: "Notes",
          udpPort: "Port UDP",
          volume: "volume",
          save: "Sauvegarder",
        },
        UploadFile: {
          hoursLeft: "{{hours}} heures restantes",
          minsLeft: "{{mins}} minutes restantes",
          secsLeft: "{{secs}} secondes restantes",
          dropFile: "Faites glisser votre fichier ici",
          orSelectFile: "ou appuyez sur selectionner le fichier",
          selectFile: "Selectionner le fichier",
          fileToImport: "Fichier à importer",
          size: "Taille",
          downloadOnComputer: "Télecharger sur l'ordinateur",
          download: "Télecharger",
          sendFile: "Envoyer le fichier",
        },
        AudioList: {
          filesOnSdCard: "Fichiers présents sur la carte SD",
          download: "Télecharger",
          play: "Lire",
          stop: "Stopper",
          suppress: "Supprimer",
          file: "Fichier",
          index: "Index",
          downloadOnComputer: "Télecharger sur l'ordinateur",
          playOnESP: "Lire sur l'ESP",
        },
        title: "Application disponible en plusieurs langues",
        label: "Selectionnez un autre langage",
        about: "À propos",
        home: "Accueil",
      },
    },
  },
});

export default i18n;

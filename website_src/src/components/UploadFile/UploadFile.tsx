import { Dropzone } from "@mantine/dropzone";
import { Data } from "../../App";
import {
  Text,
  Box,
  Button,
  Group,
  List,
  Progress,
  Table,
  useMantineTheme,
  Badge,
} from "@mantine/core";
import { IconCheck, IconPhoto, IconUpload, IconX } from "@tabler/icons-react";
import { useEffect, useRef, useState } from "react";
import { notifications } from "@mantine/notifications";
import { useTranslation } from "react-i18next";

import { parseFile } from "music-metadata";
// import { inspect } from 'util';

interface UploadFileProps {
  data?: Data;
  fetchData: () => Promise<void>;
}

function formatFileSize(file: File): string {
  const fileSize = file.size;
  const kiloByte = 1024;
  const megaByte = kiloByte * 1024;
  const gigaByte = megaByte * 1024;

  if (fileSize < kiloByte) {
    return `${fileSize} Bytes`;
  } else if (fileSize < megaByte) {
    const sizeInKB = (fileSize / kiloByte).toFixed(2);
    return `${sizeInKB} KB`;
  } else if (fileSize < gigaByte) {
    const sizeInMB = (fileSize / megaByte).toFixed(2);
    return `${sizeInMB} MB`;
  } else {
    const sizeInGB = (fileSize / gigaByte).toFixed(2);
    return `${sizeInGB} GB`;
  }
}

export const UploadFile = ({
  data,
  fetchData,
}: UploadFileProps): JSX.Element => {
  const openRef = useRef<() => void>(null);
  const [files, setFiles] = useState<File[]>([]);
  const theme = useMantineTheme();
  const uploadStartTime = useRef<Date | null>(null);

  const [uploadEstimatedTime, setUploadEstimatedTime] =
    useState<String | null>();
  const [uploadProgress, setUploadProgress] = useState(0);
  const [uploadState, setUploadState] = useState<
    "failed" | "uploading" | "done"
  >("uploading");
  const uploadColors = { failed: "red", uploading: "blue", done: "green" };
  const { t } = useTranslation();

  const handleFileDownload = (file: File) => {
    // Créez une URL pour le fichier
    const fileUrl = URL.createObjectURL(file);
    // Créez un lien de téléchargement
    const downloadLink = document.createElement("a");
    downloadLink.href = fileUrl;
    downloadLink.download = file.name;
    // Ajoutez le lien de téléchargement au document
    document.body.appendChild(downloadLink);
    // Cliquez sur le lien pour déclencher le téléchargement
    downloadLink.click();
    // Supprimez le lien du document
    document.body.removeChild(downloadLink);
    // Libérez les ressources de l'URL
    URL.revokeObjectURL(fileUrl);
  };

  const sendFile = () => {
    if (files.length === 0) {
      return;
    }
    const xmlHttp = new XMLHttpRequest();
    // xmlHttp.onreadystatechange = httpPostProcessRequest;
    setUploadState("uploading");
    uploadStartTime.current = new Date();
    xmlHttp.onreadystatechange = () => {
      if (xmlHttp.readyState === 4) {
        if (xmlHttp.status === 200) {
          console.log("Fichier uploadé avec succès !");
          setUploadState("done");
          notifications.show({
            icon: <IconCheck size="1.1rem" />,
            withBorder: true,
            autoClose: 3000,
            color: "green",
            title: `Fichier uploadé.`,
            message: "",
          });
          setFiles([]);
          fetchData();
        } else {
          console.log("Erreur lors de l'upload du fichier.");
          setUploadState("failed");
          notifications.show({
            icon: <IconX size="1.1rem" />,
            withBorder: true,
            autoClose: 5000,
            color: "red",
            title: `Erreur lors de l'upload du fichier.`,
            message: "",
          });
        }
      }
    };

    xmlHttp.upload.onprogress = (event) => {
      const progress = (event.loaded / event.total) * 100;
      setUploadProgress(progress);

      const actualTime =
        new Date().getTime() - uploadStartTime.current!.getTime();
      console.log(
        `Progression : ${progress}%`,
        new Date().getTime(),
        uploadStartTime.current!.getTime(),
        actualTime,
        actualTime / 1000
      );

      const dureeEstimeeTotale = actualTime / (progress / 100) - actualTime;
      const secsLeft = Math.floor(dureeEstimeeTotale / 1000);
      const minsLeft = Math.floor(secsLeft / 60);
      const hoursLeft = Math.floor(minsLeft / 60);

      // setUploadEstimatedTime(
      //   `${heuresRestantes} heures, ${minutesRestantes % 60} minutes et ${
      //     secondesRestantes % 60
      //   } secondes.`
      // );
      if (hoursLeft > 0)
        setUploadEstimatedTime(t("UploadFile.hoursLeft", { hours: hoursLeft }));
      else if (minsLeft > 0)
        setUploadEstimatedTime(t("UploadFile.minsLeft", { mins: minsLeft }));
      else setUploadEstimatedTime(t("UploadFile.secsLeft", { secs: secsLeft }));
    };

    var formData = new FormData();
    formData.append("data", files[0], "/" + files[0].name);
    xmlHttp.open("POST", "/edit");
    xmlHttp.send(formData);
  };

  return (
    <>
      <Dropzone
        h={200}
        multiple={false}
        openRef={openRef}
        onDrop={(files) => {
          console.log("accepted files", files);
          setFiles(files);
        }}
        onReject={(files) => console.log("rejected files", files)}
        styles={{ inner: { height: "100%" } }}
      >
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            height: "100%",
          }}
        >
          <Dropzone.Accept>
            <IconUpload
              size="3.2rem"
              stroke={1.5}
              color={
                theme.colors[theme.primaryColor][
                  theme.colorScheme === "dark" ? 4 : 6
                ]
              }
            />
          </Dropzone.Accept>
          <Dropzone.Reject>
            <IconX
              size="3.2rem"
              stroke={1.5}
              color={theme.colors.red[theme.colorScheme === "dark" ? 4 : 6]}
            />
          </Dropzone.Reject>
          <Dropzone.Idle>
            <IconPhoto size="3.2rem" stroke={1.5} />
          </Dropzone.Idle>

          <div>
            <Text size="xl" inline>
              {t("UploadFile.dropFile")}
            </Text>
            <Text size="sm" color="dimmed" inline mt={7}>
              {t("UploadFile.orSelectFile")}
            </Text>
          </div>
          {/* </Group> */}
        </Box>
      </Dropzone>

      <Group position="center" mt="md">
        <Button onClick={() => openRef.current?.()}>
          {t("UploadFile.selectFile")}
        </Button>
      </Group>

      <Table>
        <thead>
          <tr>
            <th>{t("UploadFile.fileToImport")}</th>
            <th>{t("UploadFile.size")}</th>
            <th>{t("UploadFile.downloadOnComputer")}</th>
          </tr>
        </thead>
        <tbody>
          {files.map((file, index) => (
            <tr key={index}>
              <td>{file.name}</td>
              <td>{formatFileSize(file)}</td>
              <td>
                <Badge
                  onClick={() => handleFileDownload(file)}
                  style={{ cursor: "pointer" }}
                >
                  {t("UploadFile.download")}
                </Badge>
              </td>
            </tr>
          ))}
        </tbody>
      </Table>

      <Button onClick={() => sendFile()}>{t("UploadFile.sendFile")}</Button>
      <Progress
        m={5}
        radius={"xl"}
        size={24}
        value={uploadProgress}
        label={uploadProgress.toFixed(2) + "%"}
        color={uploadColors[uploadState]}
      />
      <Text>{uploadEstimatedTime}</Text>
    </>
  );
};

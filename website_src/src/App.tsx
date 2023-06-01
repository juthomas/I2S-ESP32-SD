import { useRef, useState } from "react";
import {
  FileButton,
  Button,
  Group,
  Text,
  List,
  Progress,
  Paper,
  useMantineTheme,
  Box,
  Table,
  Badge,
  Notification,
  Title,
} from "@mantine/core";
import { Dropzone, IMAGE_MIME_TYPE } from "@mantine/dropzone";
import { IconUpload, IconPhoto, IconX, IconCheck } from "@tabler/icons-react";
import { notifications } from "@mantine/notifications";

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

function App() {
  const theme = useMantineTheme();
  const [files, setFiles] = useState<File[]>([]);
  const [uploadProgress, setUploadProgress] = useState(0);
  const [uploadState, setUploadState] = useState<
    "failed" | "uploading" | "done"
  >("uploading");
  const uploadColors = { failed: "red", uploading: "blue", done: "green" };
  const openRef = useRef<() => void>(null);

  // function httpPostProcessRequest() {

  // if (xmlHttp.readyState == 4) {
  //   if (xmlHttp.status != 200)
  //     alert("ERROR[" + xmlHttp.status + "]: " + xmlHttp.responseText);
  // }
  // }

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
      const progress = Math.round((event.loaded / event.total) * 100);
      console.log(`Progression : ${progress}%`);
      setUploadProgress(progress);
    };

    var formData = new FormData();
    formData.append("data", files[0], "/" + files[0].name);
    xmlHttp.open("POST", "/edit");
    xmlHttp.send(formData);
  };
  return (
    <Paper shadow="sm" p="md" m="lg">
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
        {/* <Group position="center"  spacing="xl" style={{  pointerEvents: 'none' }}> */}
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
              Faites glisser vos fichiers ici
            </Text>
            <Text size="sm" color="dimmed" inline mt={7}>
              ou appuyez sur selectionner les fichiers
            </Text>
          </div>
          {/* </Group> */}
        </Box>
      </Dropzone>

      <Group position="center" mt="md">
        <Button onClick={() => openRef.current?.()}>
          Selectionner les fichiers
        </Button>
      </Group>

      <Table>
        <thead>
          <tr>
            <th>Fichier à importer</th>
            <th>Taille</th>
            <th>Télecharger sur l'ordinateur</th>
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
                  Télecharger
                </Badge>
              </td>
            </tr>
          ))}
        </tbody>
      </Table>

      <List size="sm" mt={5} withPadding>
        {/*         
          <List.Item key={index} onClick={() => handleFileDownload(file)}>{file.name} {formatFileSize(file)}</List.Item>
        ))} */}
      </List>
      <Button onClick={() => sendFile()}>Envoyer le fichier</Button>
      <Progress
        m={5}
        radius={"xl"}
        size={24}
        value={uploadProgress}
        label={uploadProgress + "%"}
        color={uploadColors[uploadState]}
      />
      <Title order={4}>Fichiers présents sur la carte SD</Title>
      <Table>
        <thead>
          <tr>
            <th>Fichier</th>
            <th>Taille</th>
            <th>Type</th>
            <th>Télecharger sur l'ordinateur</th>
            <th>Lire sur l'ESP</th>
          </tr>
        </thead>
        <tbody>
          {files.map((file, index) => (
            <tr key={index}>
              <td>{file.name}</td>
              <td>{formatFileSize(file)}</td>
              <td>audio?</td>
              <td>
                <Badge
                  onClick={() => handleFileDownload(file)}
                  style={{ cursor: "pointer" }}
                >
                  Télecharger
                </Badge>
              </td>
              <td>
                <Badge
                  onClick={() => handleFileDownload(file)}
                  style={{ cursor: "pointer" }}
                >
                  Lire
                </Badge>
                <Badge
                  onClick={() => handleFileDownload(file)}
                  style={{ cursor: "pointer" }}
                >
                  Pause
                </Badge>
              </td>
            </tr>
          ))}
        </tbody>
      </Table>
    </Paper>
  );
}

export default App;

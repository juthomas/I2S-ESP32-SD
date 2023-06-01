import { useState } from "react";
import { FileButton, Button, Group, Text, List, Progress } from "@mantine/core";

function App() {
  const [files, setFiles] = useState<File[]>([]);

  const [uploadProgress, setUploadProgress] = useState(0);
  const [uploadState, setUploadState] = useState<
    "failed" | "uploading" | "done"
  >("uploading");
  const uploadColors = { failed: "red", uploading: "blue", done: "green" };

  // function httpPostProcessRequest() {

  // if (xmlHttp.readyState == 4) {
  //   if (xmlHttp.status != 200)
  //     alert("ERROR[" + xmlHttp.status + "]: " + xmlHttp.responseText);
  // }
  // }
  const sendFile = () => {
    if (files.length === 0) {
      return;
    }
    const xmlHttp = new XMLHttpRequest();
    // xmlHttp.onreadystatechange = httpPostProcessRequest;
    setUploadState('uploading');
    xmlHttp.onreadystatechange = () => {
      if (xmlHttp.readyState === 4) {
        if (xmlHttp.status === 200) {
          console.log("Fichier téléchargé avec succès !");
          setUploadState('done');
        } else {
          console.log("Erreur lors du téléchargement du fichier.");
          setUploadState('failed');
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
    <>
      <Group position="center">
        <FileButton onChange={setFiles} multiple>
          {(props) => <Button {...props}>Upload image</Button>}
        </FileButton>
      </Group>

      {files.length > 0 && (
        <Text size="sm" mt="sm">
          Picked files:
        </Text>
      )}

      <List size="sm" mt={5} withPadding>
        {files.map((file, index) => (
          <List.Item key={index}>{file.name}</List.Item>
        ))}
      </List>
      <Button onClick={() => sendFile()}>SEND</Button>
      <Progress
        m={5}
        radius={"xl"}
        size={24}
        value={uploadProgress}
        label={uploadProgress + "%"}
        color={uploadColors[uploadState]}
      />
    </>
  );
}

export default App;

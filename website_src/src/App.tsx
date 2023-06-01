import { useState } from 'react';
import { FileButton, Button, Group, Text, List } from '@mantine/core';

function App() {
  const [files, setFiles] = useState<File[]>([]);


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
    var formData = new FormData();
    formData.append("data", files[0], '/filename');
    xmlHttp.open("POST", "/edit");
    xmlHttp.send(formData);
  }
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
    </>
  );
}

export default App;

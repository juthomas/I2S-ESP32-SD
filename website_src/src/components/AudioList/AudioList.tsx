import { ActionIcon, Badge, Box, Table, Title } from "@mantine/core";
import { IconRefresh } from "@tabler/icons-react";
import axios from "axios";
import { Data } from "../../App";

const handleLinkDownload = (link: string) => {
  // Créez une URL pour le fichier
  // const fileUrl = URL.createObjectURL(file);
  // Créez un lien de téléchargement
  const downloadLink = document.createElement("a");
  downloadLink.href = link;
  downloadLink.download = link;
  // Ajoutez le lien de téléchargement au document
  document.body.appendChild(downloadLink);
  // Cliquez sur le lien pour déclencher le téléchargement
  downloadLink.click();
  // Supprimez le lien du document
  document.body.removeChild(downloadLink);
};

interface AudioListProps {
  data?: Data;
  fetchData: () => Promise<void>;
}

export const AudioList = ({ data, fetchData }: AudioListProps): JSX.Element => {
  return (
    <>
      <Box sx={{ display: "flex", justifyContent: "space-between" }}>
        <Title order={4}>Fichiers présents sur la carte SD</Title>
        <ActionIcon variant="filled" color="blue" onClick={() => fetchData()}>
          <IconRefresh />
        </ActionIcon>
      </Box>

      <Table>
        <thead>
          <tr>
            <th>Fichier</th>
            <th>Index</th>
            <th>Télecharger sur l'ordinateur</th>
            <th>Lire sur l'ESP</th>
            <th>Supprimer</th>
          </tr>
        </thead>
        <tbody>
          {data?.track_assignation.map((element, index) => (
            <tr key={index}>
              <td>{element.path.substring(1)}</td>
              <td>{element.index}</td>
              <td>
                <Badge
                  onClick={() => handleLinkDownload(element.path)}
                  style={{ cursor: "pointer" }}
                >
                  Télecharger
                </Badge>
              </td>
              <td>
                <Badge
                  onClick={() => axios.post("/play", { index })}
                  style={{ cursor: "pointer" }}
                >
                  Lire
                </Badge>
                <Badge
                  onClick={() => axios.post("/stop", { index })}
                  style={{ cursor: "pointer" }}
                >
                  Stopper
                </Badge>
              </td>
              <td>
                <Badge
                  color="red"
                  onClick={() =>
                    axios.post("/delete", { index }).then(() => fetchData())
                  }
                  style={{ cursor: "pointer" }}
                >
                  Supprimer
                </Badge>
              </td>
            </tr>
          ))}
        </tbody>
      </Table>
    </>
  );
};

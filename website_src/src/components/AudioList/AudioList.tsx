import {
  ActionIcon,
  Badge,
  Box,
  Group,
  ScrollArea,
  Table,
  Text,
  Title,
} from "@mantine/core";
import {
  IconArrowDown,
  IconArrowUp,
  IconGripVertical,
  IconRefresh,
} from "@tabler/icons-react";
import axios from "axios";
import { Data } from "../../App";
import { useTranslation } from "react-i18next";
import { useEffect, useState } from "react";
import { useMediaQuery } from "@mantine/hooks";

const handleLinkDownload = (link: string) => {
  const encodedPath = link
    .split("/")
    .map((segment, index) => (index === 0 ? segment : encodeURIComponent(segment)))
    .join("/");
  const suggestedName = link.split("/").filter(Boolean).pop() ?? "audio-file";
  const downloadLink = document.createElement("a");
  downloadLink.href = encodedPath;
  downloadLink.download = suggestedName;
  document.body.appendChild(downloadLink);
  downloadLink.click();
  document.body.removeChild(downloadLink);
};

interface AudioListProps {
  data?: Data;
  fetchData: () => Promise<void>;
}

export const AudioList = ({ data, fetchData }: AudioListProps): JSX.Element => {
  const { t } = useTranslation();
  const isMobile = useMediaQuery("(max-width: 768px)");
  const [tracks, setTracks] = useState<Data["track_assignation"]>([]);
  const [draggedIndex, setDraggedIndex] = useState<number | null>(null);
  const [isReordering, setIsReordering] = useState(false);

  useEffect(() => {
    setTracks(data?.track_assignation ?? []);
  }, [data?.track_assignation]);

  const normalizeTracks = (
    items: Data["track_assignation"]
  ): Data["track_assignation"] =>
    items.map((item, index) => ({ ...item, index }));

  const moveTrack = (
    fromIndex: number,
    toIndex: number
  ): Data["track_assignation"] => {
    if (fromIndex < 0 || toIndex < 0 || fromIndex >= tracks.length || toIndex >= tracks.length) {
      return tracks;
    }
    const updated = [...tracks];
    const [moved] = updated.splice(fromIndex, 1);
    updated.splice(toIndex, 0, moved);
    return normalizeTracks(updated);
  };

  const persistTrackOrder = async (updatedTracks: Data["track_assignation"]) => {
    setTracks(updatedTracks);
    setIsReordering(true);
    try {
      await axios.post("/reorder", {
        paths: updatedTracks.map((track) => track.path),
      });
      await fetchData();
    } catch (error) {
      console.error("Unable to reorder tracks", error);
      setTracks(data?.track_assignation ?? updatedTracks);
    } finally {
      setIsReordering(false);
    }
  };

  const handleDrop = async (targetIndex: number) => {
    if (draggedIndex === null || draggedIndex === targetIndex) {
      setDraggedIndex(null);
      return;
    }
    const updated = moveTrack(draggedIndex, targetIndex);
    setDraggedIndex(null);
    await persistTrackOrder(updated);
  };

  const handleMoveByStep = async (index: number, step: number) => {
    const nextIndex = index + step;
    if (nextIndex < 0 || nextIndex >= tracks.length) return;
    const updated = moveTrack(index, nextIndex);
    await persistTrackOrder(updated);
  };

  return (
    <>
      <Box sx={{ display: "flex", justifyContent: "space-between" }}>
        <Title order={4}>{t("AudioList.filesOnSdCard")}</Title>
        <ActionIcon
          variant="light"
          color="blue"
          onClick={() => fetchData()}
          disabled={isReordering}
          title={t("AudioList.refresh")}
        >
          <IconRefresh />
        </ActionIcon>
      </Box>
      <Text size="sm" c="dimmed" mt="xs">
        {t("AudioList.reorderHint")}
      </Text>
      {tracks.length === 0 ? (
        <Text size="sm" c="dimmed" mt="sm">
          {t("AudioList.emptyState")}
        </Text>
      ) : (
        <ScrollArea>
          <Table verticalSpacing="sm" highlightOnHover striped withBorder>
            <thead>
              <tr>
                <th>{t("AudioList.reorder")}</th>
                <th>{t("AudioList.file")}</th>
                <th>{t("AudioList.index")}</th>
                <th>{t("AudioList.downloadOnComputer")}</th>
                <th>{t("AudioList.playOnESP")}</th>
                <th>{t("AudioList.suppress")}</th>
              </tr>
            </thead>
            <tbody>
              {tracks.map((element, index) => (
                <tr
                  key={element.path}
                  draggable={!isMobile && !isReordering}
                  onDragStart={() => setDraggedIndex(index)}
                  onDragOver={(event) => event.preventDefault()}
                  onDrop={() => void handleDrop(index)}
                  style={{ opacity: draggedIndex === index ? 0.5 : 1 }}
                >
                  <td>
                    <Group spacing={4} noWrap>
                      {!isMobile && <IconGripVertical size="1rem" />}
                      <ActionIcon
                        size="sm"
                        variant="subtle"
                        disabled={index === 0 || isReordering}
                        onClick={() => void handleMoveByStep(index, -1)}
                        title={t("AudioList.moveUp")}
                      >
                        <IconArrowUp size="1rem" />
                      </ActionIcon>
                      <ActionIcon
                        size="sm"
                        variant="subtle"
                        disabled={index === tracks.length - 1 || isReordering}
                        onClick={() => void handleMoveByStep(index, 1)}
                        title={t("AudioList.moveDown")}
                      >
                        <IconArrowDown size="1rem" />
                      </ActionIcon>
                    </Group>
                  </td>
                  <td>{element.path.substring(1)}</td>
                  <td>{element.index}</td>
                  <td>
                    <Badge
                      variant="light"
                      onClick={() => handleLinkDownload(element.path)}
                      style={{ cursor: "pointer" }}
                    >
                      {t("AudioList.download")}
                    </Badge>
                  </td>
                  <td>
                    <Group spacing={6} noWrap>
                      <Badge
                        variant="light"
                        color="green"
                        onClick={() => axios.post("/play", { index: element.index })}
                        style={{ cursor: "pointer" }}
                      >
                        {t("AudioList.play")}
                      </Badge>
                      <Badge
                        variant="light"
                        color="yellow"
                        onClick={() => axios.post("/stop", { index: element.index })}
                        style={{ cursor: "pointer" }}
                      >
                        {t("AudioList.stop")}
                      </Badge>
                    </Group>
                  </td>
                  <td>
                    <Badge
                      color="red"
                      variant="light"
                      onClick={() =>
                        axios.post("/delete", { index: element.index }).then(() => fetchData())
                      }
                      style={{ cursor: "pointer" }}
                    >
                      {t("AudioList.suppress")}
                    </Badge>
                  </td>
                </tr>
              ))}
            </tbody>
          </Table>
        </ScrollArea>
      )}
    </>
  );
};

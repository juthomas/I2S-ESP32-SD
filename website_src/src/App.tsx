import { useEffect, useState } from "react";
import { Text, Paper, useMantineTheme, Title } from "@mantine/core";
import { Settings } from "./components/Settings";
import { AudioList } from "./components/AudioList";
import { UploadFile } from "./components/UploadFile";
import { LanguageSelection } from "./components/LanguageSelection";

interface TrackAssignation {
  path: string;
  index: number;
}

export interface Data {
  loop_file: boolean;
  auto_play: boolean;
  note: string;
  udp_port: number;
  volume: number;
  track_assignation: TrackAssignation[];
}

function App() {
  const theme = useMantineTheme();

  const [data, setData] = useState<Data>();

  const fetchData = async () => {
    try {
      const response = await fetch("/data");
      console.log("Response :", response);
      if (response.ok) {
        // setData({
        //   loop_file: true,
        //   auto_play: false,
        //   note: "notes pour plus tard...",
        //   udp_port: 8266,
        //   volume: 60,
        //   track_assignation: [
        //     {
        //       path: "/osc.wav",
        //       index: 1,
        //     },
        //     {
        //       path: "/music2.wav",
        //       index: 2,
        //     },
        //   ],
        // });
        const data: Data = await response.json();
        setData(data);
        console.log("Fetched data :", data);
      } else {
        console.error("Failed to fetch sensor data");
      }
    } catch (error) {
      console.error("Error fetching sensor data", error);
    }
  };

  useEffect(() => {
    fetchData();
  }, []);

  return (
    <Paper shadow="sm" p="md" m="lg">
      <Settings data={data} fetchData={fetchData} />
      <LanguageSelection />
      <Title order={3}>Note :</Title>
      <Text>{data?.note}</Text>

      <UploadFile data={data} fetchData={fetchData} />
      <AudioList data={data} fetchData={fetchData} />
    </Paper>
  );
}

export default App;

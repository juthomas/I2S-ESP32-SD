import { useEffect, useState } from "react";
import {
  Text,
  Paper,
  useMantineTheme,
  Title,
  AppShell,
  Navbar,
  Header,
  useMantineColorScheme,
  ActionIcon,
} from "@mantine/core";
import { Settings } from "./components/Settings";
import { AudioList } from "./components/AudioList";
import { UploadFile } from "./components/UploadFile";
import { LanguageSelection } from "./components/LanguageSelection";
import { IconMoonStars, IconSun } from "@tabler/icons-react";

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
  const { colorScheme, toggleColorScheme } = useMantineColorScheme();
  const dark = colorScheme === "dark";

  useEffect(() => {
    console.log("color:", theme.colorScheme, colorScheme);
  }, [theme, colorScheme]);

  return (
    <AppShell
      styles={{
        main: {
          background:
            colorScheme === "dark"
              ? theme.colors.dark[8]
              : theme.colors.gray[0],
        },
      }}
      header={
        <Header height={{ base: 50, md: 70 }} p="md">
          <div
            style={{ display: "flex", alignItems: "center", height: "100%" }}
          >
            {/* <MediaQuery largerThan="sm" styles={{ display: 'none' }}>
          <Burger
            opened={opened}
            onClick={() => setOpened((o) => !o)}
            size="sm"
            color={theme.colors.gray[6]}
            mr="xl"
          />
        </MediaQuery> */}

            <Text color={ colorScheme === "light" ? "blue" : "red"}>Application header</Text>
            <ActionIcon
              variant="outline"
              color={dark ? "yellow" : "blue"}
              onClick={() => toggleColorScheme()}
              title="Toggle color scheme"
            >
              {dark ? (
                <IconSun size="1.1rem" />
              ) : (
                <IconMoonStars size="1.1rem" />
              )}
            </ActionIcon>
          </div>
        </Header>
      }
    >
      <Paper shadow="sm" p="md" m="lg">
        <Settings data={data} fetchData={fetchData} />
        <LanguageSelection />
        <Title order={3}>Note :</Title>
        <Text>{data?.note}</Text>

        <UploadFile data={data} fetchData={fetchData} />
        <AudioList data={data} fetchData={fetchData} />
      </Paper>
    </AppShell>
  );
}

export default App;

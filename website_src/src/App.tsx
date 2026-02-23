import { useEffect, useState } from "react";
import {
  ActionIcon,
  AppShell,
  Badge,
  Box,
  Center,
  Container,
  Group,
  Header,
  Loader,
  Paper,
  Stack,
  Text,
  Title,
  useMantineColorScheme,
  useMantineTheme,
} from "@mantine/core";
import { useMediaQuery } from "@mantine/hooks";
import { Settings } from "./components/Settings";
import { AudioList } from "./components/AudioList";
import { UploadFile } from "./components/UploadFile";
import { LanguageSelection } from "./components/LanguageSelection";
import {
  IconMoonStars,
  IconRefresh,
  IconSun,
  IconWifi,
} from "@tabler/icons-react";

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
  ap_ssid?: string;
  ap_ip?: string;
  ap_name?: string;
  ap_password?: string;
  ap_ip_config?: string;
  esp_now_channel?: number;
  device_mode?: number;
  mesh_ttl?: number;
  ap_safety_timeout_s?: number;
  ap_enabled?: boolean;
  ap_runtime_enabled?: boolean;
  button_gpio13_track?: number;
  button_gpio16_track?: number;
  track_assignation: TrackAssignation[];
}

function App() {
  const theme = useMantineTheme();
  const { colorScheme, toggleColorScheme } = useMantineColorScheme();
  const dark = colorScheme === "dark";
  const isMobile = useMediaQuery("(max-width: 768px)");

  const [data, setData] = useState<Data>();
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const wifiName = data?.ap_ssid?.trim() ? data.ap_ssid : "Unavailable";

  const fetchData = async () => {
    setError(null);
    try {
      const response = await fetch("/data");
      console.log("Response :", response);
      if (response.ok) {
        const responseData: Data = await response.json();
        setData(responseData);
        console.log("Fetched data :", responseData);
      } else {
        setError("Failed to load device data.");
      }
    } catch (error) {
      console.error("Error fetching sensor data", error);
      setError("Unable to reach the device.");
    } finally {
      setIsLoading(false);
    }
  };

  useEffect(() => {
    void fetchData();
  }, []);

  return (
    <AppShell
      padding={0}
      styles={{
        main: {
          background: dark
            ? "linear-gradient(180deg, #0b1220 0%, #111827 100%)"
            : theme.colors.gray[0],
          minHeight: "100vh",
        },
      }}
      header={
        <Header
          height={{ base: 108, md: 72 }}
          p="md"
          sx={{
            backgroundColor: dark ? theme.colors.dark[7] : theme.white,
            borderBottom: `1px solid ${
              dark ? theme.colors.dark[4] : theme.colors.gray[3]
            }`,
          }}
        >
          {isMobile ? (
            <Stack spacing={6} sx={{ height: "100%" }}>
              <Group position="apart" noWrap>
                <Box sx={{ minWidth: 0 }}>
                  <Title order={4} sx={{ lineHeight: 1.1 }}>
                    I2S SD Controller
                  </Title>
                </Box>
                <Group spacing={6} noWrap>
                  <ActionIcon
                    size="md"
                    variant="outline"
                    color="blue"
                    onClick={() => void fetchData()}
                    title="Refresh"
                  >
                    <IconRefresh size="0.95rem" />
                  </ActionIcon>
                  <ActionIcon
                    size="md"
                    variant="outline"
                    color={dark ? "yellow" : "blue"}
                    onClick={() => toggleColorScheme()}
                    title="Toggle color scheme"
                  >
                    {dark ? (
                      <IconSun size="0.95rem" />
                    ) : (
                      <IconMoonStars size="0.95rem" />
                    )}
                  </ActionIcon>
                </Group>
              </Group>
              <Group position="apart" noWrap sx={{ minWidth: 0 }}>
                <Badge
                  variant={data?.ap_runtime_enabled ? "filled" : "outline"}
                  color={data?.ap_runtime_enabled ? "teal" : "gray"}
                  leftSection={<IconWifi size={11} />}
                  sx={{
                    flex: 1,
                    minWidth: 0,
                    overflow: "hidden",
                    textOverflow: "ellipsis",
                    whiteSpace: "nowrap",
                  }}
                  title={wifiName}
                >
                  {wifiName}
                </Badge>
                <LanguageSelection compact width={90} size="xs" />
              </Group>
            </Stack>
          ) : (
            <Group position="apart" sx={{ height: "100%", flexWrap: "nowrap" }}>
              <Box>
                <Title order={3}>I2S SD Controller</Title>
                <Text size="xs" color="dimmed">
                  Pilotage multi-enceintes ESP32
                </Text>
              </Box>
              <Group spacing="xs" noWrap>
                <Badge
                  variant={data?.ap_runtime_enabled ? "filled" : "outline"}
                  color={data?.ap_runtime_enabled ? "teal" : "gray"}
                  leftSection={<IconWifi size={12} />}
                  sx={{
                    maxWidth: 200,
                    overflow: "hidden",
                    textOverflow: "ellipsis",
                    whiteSpace: "nowrap",
                  }}
                  title={wifiName}
                >
                  {wifiName}
                </Badge>
                <LanguageSelection compact width={120} size="xs" />
                <ActionIcon
                  variant="outline"
                  color="blue"
                  onClick={() => void fetchData()}
                  title="Refresh"
                >
                  <IconRefresh size="1rem" />
                </ActionIcon>
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
              </Group>
            </Group>
          )}
        </Header>
      }
    >
      <Container size="lg" py="md">
        <Stack spacing="md">
          <Paper
            shadow="sm"
            withBorder
            p="md"
            radius="md"
            sx={{
              backgroundColor: dark ? theme.colors.dark[7] : theme.white,
              borderColor: dark ? theme.colors.dark[4] : theme.colors.gray[3],
            }}
          >
            <Group position="apart" align="flex-start">
              <Box>
                <Title order={4}>Dashboard</Title>
                <Text size="sm" color="dimmed">
                  Réglages, upload et contrôle audio centralisés.
                </Text>
              </Box>
              <Settings data={data} fetchData={fetchData} />
            </Group>

            <Group mt="sm" spacing="xs">
              <Badge color={data?.ap_runtime_enabled ? "green" : "gray"} variant="light">
                {data?.ap_runtime_enabled ? "AP actif" : "AP inactif"}
              </Badge>
              <Badge color="blue" variant="light">
                {`Tracks: ${data?.track_assignation?.length ?? 0}`}
              </Badge>
            </Group>

            {data?.note && (
              <Text mt="sm">
                <Text span fw={600}>
                  Note:{" "}
                </Text>
                {data.note}
              </Text>
            )}
          </Paper>

          {isLoading ? (
            <Paper
              withBorder
              p="xl"
              radius="md"
              sx={{
                backgroundColor: dark ? theme.colors.dark[7] : theme.white,
                borderColor: dark ? theme.colors.dark[4] : theme.colors.gray[3],
              }}
            >
              <Center>
                <Loader />
              </Center>
            </Paper>
          ) : error ? (
            <Paper
              withBorder
              p="md"
              radius="md"
              sx={{
                backgroundColor: dark ? theme.colors.dark[7] : theme.white,
                borderColor: dark ? theme.colors.dark[4] : theme.colors.gray[3],
              }}
            >
              <Text color="red">{error}</Text>
            </Paper>
          ) : (
            <>
              <Paper
                shadow="sm"
                withBorder
                p="md"
                radius="md"
                sx={{
                  backgroundColor: dark ? theme.colors.dark[7] : theme.white,
                  borderColor: dark ? theme.colors.dark[4] : theme.colors.gray[3],
                }}
              >
                <UploadFile data={data} fetchData={fetchData} />
              </Paper>
              <Paper
                shadow="sm"
                withBorder
                p="md"
                radius="md"
                sx={{
                  backgroundColor: dark ? theme.colors.dark[7] : theme.white,
                  borderColor: dark ? theme.colors.dark[4] : theme.colors.gray[3],
                }}
              >
                <AudioList data={data} fetchData={fetchData} />
              </Paper>
            </>
          )}
        </Stack>
      </Container>
    </AppShell>
  );
}

export default App;

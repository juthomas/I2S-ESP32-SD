import { useCallback, useEffect, useRef, useState } from "react";
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
  IconWifiOff,
} from "@tabler/icons-react";
import { useTranslation } from "react-i18next";
import { notifications } from "@mantine/notifications";

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
  allow_play_over_playing?: boolean;
  ap_runtime_enabled?: boolean;
  button_gpio13_track?: number;
  button_gpio16_track?: number;
  button_gpio13_pull_mode?: number;
  button_gpio16_pull_mode?: number;
  button_gpio13_active_level?: number;
  button_gpio16_active_level?: number;
  track_assignation: TrackAssignation[];
}

type ConnectionState = "checking" | "online" | "offline";

function App() {
  const { t } = useTranslation();
  const theme = useMantineTheme();
  const { colorScheme, toggleColorScheme } = useMantineColorScheme();
  const dark = colorScheme === "dark";
  const isMobile = useMediaQuery("(max-width: 768px)");

  const [data, setData] = useState<Data>();
  const [isLoading, setIsLoading] = useState(true);
  const [errorKey, setErrorKey] = useState<string | null>(null);
  const [connectionState, setConnectionState] = useState<ConnectionState>("checking");
  const healthCheckInFlightRef = useRef(false);
  const dataFetchInFlightRef = useRef(false);
  const previousConnectionStateRef = useRef<ConnectionState>("checking");
  const wifiName = data?.ap_ssid?.trim() ? data.ap_ssid : t("App.unavailable");
  const connectionStatusLabel =
    connectionState === "online"
      ? t("App.connectionOnline")
      : connectionState === "offline"
      ? t("App.connectionOffline")
      : t("App.connectionChecking");
  const connectionStatusColor =
    connectionState === "online" ? "green" : connectionState === "offline" ? "red" : "gray";

  const fetchData = useCallback(
    async (silent = false, updateConnectionState = true) => {
      if (dataFetchInFlightRef.current) {
        return;
      }
      dataFetchInFlightRef.current = true;
      if (!silent) {
        setErrorKey(null);
      }
      try {
        const response = await fetch(`/data?ts=${Date.now()}`, { cache: "no-store" });
        if (response.ok) {
          const responseData: Data = await response.json();
          setData(responseData);
          if (updateConnectionState) {
            setConnectionState("online");
          }
        } else {
          if (!silent) {
            setErrorKey("App.errorLoadData");
          }
          if (updateConnectionState) {
            setConnectionState("offline");
          }
        }
      } catch (error) {
        console.error("Error fetching sensor data", error);
        if (!silent) {
          setErrorKey("App.errorReachDevice");
        }
        if (updateConnectionState) {
          setConnectionState("offline");
        }
      } finally {
        setIsLoading(false);
        dataFetchInFlightRef.current = false;
      }
    },
    []
  );

  const checkServerHealth = useCallback(async () => {
    if (healthCheckInFlightRef.current) {
      return;
    }
    healthCheckInFlightRef.current = true;
    const controller = new AbortController();
    const timeoutId = window.setTimeout(() => controller.abort(), 2500);
    try {
      let response = await fetch("/health", {
        cache: "no-store",
        signal: controller.signal,
      });
      if (response.status === 404) {
        response = await fetch("/data", {
          cache: "no-store",
          signal: controller.signal,
        });
      }
      if (!response.ok) {
        throw new Error("Health check failed");
      }
      setConnectionState("online");
    } catch (error) {
      setConnectionState("offline");
    } finally {
      window.clearTimeout(timeoutId);
      healthCheckInFlightRef.current = false;
    }
  }, []);

  useEffect(() => {
    void fetchData();
  }, [fetchData]);

  useEffect(() => {
    void checkServerHealth();
    const intervalId = window.setInterval(() => {
      void checkServerHealth();
    }, 3000);
    return () => window.clearInterval(intervalId);
  }, [checkServerHealth]);

  useEffect(() => {
    if (connectionState !== "online") {
      return;
    }
    const intervalId = window.setInterval(() => {
      void fetchData(true, false);
    }, 3000);
    return () => window.clearInterval(intervalId);
  }, [connectionState, fetchData]);

  useEffect(() => {
    const previousState = previousConnectionStateRef.current;
    if (previousState === "online" && connectionState === "offline") {
      notifications.show({
        color: "red",
        withBorder: true,
        autoClose: 3500,
        icon: <IconWifiOff size="1rem" />,
        title: t("App.connectionLostTitle"),
        message: t("App.connectionLostMessage"),
      });
    }
    if (previousState === "offline" && connectionState === "online") {
      notifications.show({
        color: "green",
        withBorder: true,
        autoClose: 2500,
        icon: <IconWifi size="1rem" />,
        title: t("App.connectionRestoredTitle"),
        message: t("App.connectionRestoredMessage"),
      });
      void fetchData(false, false);
    }
    previousConnectionStateRef.current = connectionState;
  }, [connectionState, t]);

  useEffect(() => {
    const handleOffline = () => setConnectionState("offline");
    const handleOnline = () => {
      void checkServerHealth();
    };
    window.addEventListener("offline", handleOffline);
    window.addEventListener("online", handleOnline);
    return () => {
      window.removeEventListener("offline", handleOffline);
      window.removeEventListener("online", handleOnline);
    };
  }, [checkServerHealth]);

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
                    {t("App.controllerTitle")}
                  </Title>
                </Box>
                <Group spacing={6} noWrap>
                  <ActionIcon
                    size="md"
                    variant="outline"
                    color="blue"
                    onClick={() => void fetchData()}
                    title={t("App.refresh")}
                  >
                    <IconRefresh size="0.95rem" />
                  </ActionIcon>
                  <ActionIcon
                    size="md"
                    variant="outline"
                    color={dark ? "yellow" : "blue"}
                    onClick={() => toggleColorScheme()}
                    title={t("App.toggleColorScheme")}
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
              <Text size="xs" c={connectionStatusColor}>
                {t("App.connectionStatus")}: {connectionStatusLabel}
              </Text>
            </Stack>
          ) : (
            <Group position="apart" sx={{ height: "100%", flexWrap: "nowrap" }}>
              <Box>
                <Title order={3}>{t("App.controllerTitle")}</Title>
                <Text size="xs" color="dimmed">
                  {t("App.controllerSubtitle")}
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
                <Badge color={connectionStatusColor} variant="light">
                  {t("App.connectionStatus")}: {connectionStatusLabel}
                </Badge>
                <LanguageSelection compact width={120} size="xs" />
                <ActionIcon
                  variant="outline"
                  color="blue"
                  onClick={() => void fetchData()}
                  title={t("App.refresh")}
                >
                  <IconRefresh size="1rem" />
                </ActionIcon>
                <ActionIcon
                  variant="outline"
                  color={dark ? "yellow" : "blue"}
                  onClick={() => toggleColorScheme()}
                  title={t("App.toggleColorScheme")}
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
                <Title order={4}>{t("App.dashboardTitle")}</Title>
                <Text size="sm" color="dimmed">
                  {t("App.dashboardSubtitle")}
                </Text>
              </Box>
              <Settings data={data} fetchData={fetchData} />
            </Group>

            <Group mt="sm" spacing="xs">
              <Badge color={data?.ap_runtime_enabled ? "green" : "gray"} variant="light">
                {data?.ap_runtime_enabled ? t("App.apActive") : t("App.apInactive")}
              </Badge>
              <Badge color="blue" variant="light">
                {t("App.tracksCount", { count: data?.track_assignation?.length ?? 0 })}
              </Badge>
            </Group>

            {data?.note && (
              <Text mt="sm">
                <Text span fw={600}>
                  {t("App.noteLabel")}{" "}
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
          ) : errorKey ? (
            <Paper
              withBorder
              p="md"
              radius="md"
              sx={{
                backgroundColor: dark ? theme.colors.dark[7] : theme.white,
                borderColor: dark ? theme.colors.dark[4] : theme.colors.gray[3],
              }}
            >
              <Text color="red">{t(errorKey)}</Text>
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

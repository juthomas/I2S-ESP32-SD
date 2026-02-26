import {
  ActionIcon,
  Button,
  Flex,
  Group,
  Modal,
  NumberInput,
  Select,
  Switch,
  Tabs,
  Text,
  TextInput,
  Tooltip,
} from "@mantine/core";
import { useForm } from "@mantine/form";
import { useDisclosure, useMediaQuery } from "@mantine/hooks";
import { IconSettings } from "@tabler/icons-react";
import axios from "axios";
import { Data } from "../../App";
import { useEffect, useState } from "react";
import { useTranslation } from "react-i18next";

interface SettingsProps {
  data?: Data;
  fetchData: () => Promise<void>;
}

export const Settings = ({ data, fetchData }: SettingsProps): JSX.Element => {
  const { t } = useTranslation();
  const isMobile = useMediaQuery("(max-width: 768px)");
  const [opened, { open, close }] = useDisclosure(false);
  const [simulatingButton, setSimulatingButton] = useState<number | null>(null);
  const form = useForm({
    initialValues: {
      loop_file: data?.loop_file,
      auto_play: data?.auto_play,
      allow_play_over_playing: data?.allow_play_over_playing ?? false,
      note: data?.note,
      udp_port: data?.udp_port,
      volume: data?.volume,
      ap_ssid: data?.ap_ssid,
      ap_password: data?.ap_password,
      ap_ip_config: data?.ap_ip_config,
      esp_now_channel: data?.esp_now_channel,
      device_mode: data?.device_mode ?? 0,
      mesh_ttl: data?.mesh_ttl ?? 3,
      ap_safety_timeout_s: data?.ap_safety_timeout_s ?? 300,
      button_gpio13_track: data?.button_gpio13_track,
      button_gpio16_track: data?.button_gpio16_track,
      button_gpio13_pull_mode: data?.button_gpio13_pull_mode ?? 0,
      button_gpio16_pull_mode: data?.button_gpio16_pull_mode ?? 0,
      button_gpio13_active_level: data?.button_gpio13_active_level ?? 0,
      button_gpio16_active_level: data?.button_gpio16_active_level ?? 0,
    },
  });

  useEffect(() => {
    form.setValues({
      loop_file: data?.loop_file,
      auto_play: data?.auto_play,
      allow_play_over_playing: data?.allow_play_over_playing ?? false,
      note: data?.note,
      udp_port: data?.udp_port,
      volume: data?.volume,
      ap_ssid: data?.ap_ssid,
      ap_password: data?.ap_password,
      ap_ip_config: data?.ap_ip_config,
      esp_now_channel: data?.esp_now_channel,
      device_mode: data?.device_mode ?? 0,
      mesh_ttl: data?.mesh_ttl ?? 3,
      ap_safety_timeout_s: data?.ap_safety_timeout_s ?? 300,
      button_gpio13_track: data?.button_gpio13_track,
      button_gpio16_track: data?.button_gpio16_track,
      button_gpio13_pull_mode: data?.button_gpio13_pull_mode ?? 0,
      button_gpio16_pull_mode: data?.button_gpio16_pull_mode ?? 0,
      button_gpio13_active_level: data?.button_gpio13_active_level ?? 0,
      button_gpio16_active_level: data?.button_gpio16_active_level ?? 0,
    });
  }, [data]);

  const modeValue = String(form.values.device_mode ?? 0);
  const apDisabledMode = modeValue === "2" || modeValue === "3";
  const meshTtlDisabled = modeValue !== "1";
  const currentApSsid = data?.ap_ssid?.trim() ? data.ap_ssid : "-";
  const currentApIp = data?.ap_ip?.trim() ? data.ap_ip : "-";

  const triggerPhysicalButton = async (gpio: number) => {
    setSimulatingButton(gpio);
    try {
      await axios.post("/simulate_button", { gpio });
    } catch (error) {
      console.error("Unable to simulate button", error);
    } finally {
      setSimulatingButton(null);
    }
  };

  return (
    <>
      <Modal
        opened={opened}
        onClose={close}
        title={t("Parameters.parameters")}
        centered={!isMobile}
        fullScreen={Boolean(isMobile)}
        size={isMobile ? "100%" : "lg"}
        radius="md"
      >
        <form
          onSubmit={form.onSubmit(() => {
            console.log("Form Values", form.values);
            axios.post("/settings", form.values).then(() => fetchData());
            // window.electron.ipcRenderer.send('set-settings', message)
          })}
        >
          <Tabs defaultValue="general" keepMounted={false}>
            <Tabs.List grow>
              <Tabs.Tab value="general">{t("Parameters.generalTab")}</Tabs.Tab>
              <Tabs.Tab value="network">{t("Parameters.networkTab")}</Tabs.Tab>
              <Tabs.Tab value="buttons">{t("Parameters.buttonsTab")}</Tabs.Tab>
            </Tabs.List>

            <Tabs.Panel value="general" pt="md">
              <Switch
                labelPosition="left"
                label={t("Parameters.loopAudio")}
                {...form.getInputProps("loop_file", { type: "checkbox" })}
              />
              <Switch
                mt="md"
                labelPosition="left"
                label={t("Parameters.autoPlay")}
                {...form.getInputProps("auto_play", { type: "checkbox" })}
              />
              <Switch
                mt="md"
                labelPosition="left"
                label={t("Parameters.allowPlayOverPlaying")}
                {...form.getInputProps("allow_play_over_playing", { type: "checkbox" })}
              />
              <TextInput
                mt="md"
                label={t("Parameters.notes")}
                placeholder={String(t("Parameters.notesPlaceholder"))}
                {...form.getInputProps("note")}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.volume")}
                max={255}
                min={0}
                {...form.getInputProps("volume")}
              />
            </Tabs.Panel>

            <Tabs.Panel value="network" pt="md">
              <Select
                mt="md"
                label={t("Parameters.deviceMode")}
                data={[
                  { value: "0", label: String(t("Parameters.modeCurrent")) },
                  { value: "1", label: String(t("Parameters.modeMesh")) },
                  { value: "2", label: String(t("Parameters.modeApOff")) },
                  { value: "3", label: String(t("Parameters.modeRelayOnly")) },
                ]}
                value={modeValue}
                onChange={(value) => form.setFieldValue("device_mode", Number(value ?? 0))}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.udpPort")}
                max={99999}
                min={0}
                {...form.getInputProps("udp_port")}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.meshTtl")}
                max={8}
                min={1}
                disabled={meshTtlDisabled}
                {...form.getInputProps("mesh_ttl")}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.meshTtlHint")}
              </Text>
              <NumberInput
                mt="md"
                label={t("Parameters.apSafetyTimeout")}
                max={3600}
                min={0}
                {...form.getInputProps("ap_safety_timeout_s")}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.apSafetyTimeoutHint")}
              </Text>
              <TextInput
                mt="md"
                label={t("Parameters.apSsid")}
                placeholder="I2S-SD-DEFAULT"
                {...form.getInputProps("ap_ssid")}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.apCurrentSsid")}:{" "}
                <Text span c="inherit" fw={600}>
                  {currentApSsid}
                </Text>
              </Text>
              <TextInput
                mt="md"
                label={t("Parameters.apPassword")}
                placeholder={String(t("Parameters.apPasswordPlaceholder"))}
                {...form.getInputProps("ap_password")}
              />
              <TextInput
                mt="md"
                label={t("Parameters.apIpConfig")}
                placeholder="192.168.4.1"
                {...form.getInputProps("ap_ip_config")}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.apCurrentIp")}:{" "}
                <Text span c="inherit" fw={600}>
                  {currentApIp}
                </Text>
              </Text>
              <NumberInput
                mt="md"
                label={t("Parameters.espNowChannel")}
                max={13}
                min={1}
                {...form.getInputProps("esp_now_channel")}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {data?.ap_runtime_enabled
                  ? t("Parameters.apRuntimeOn")
                  : t("Parameters.apRuntimeOff")}
              </Text>
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.networkRestartHint")}
              </Text>
              {apDisabledMode && (
                <Text size="sm" c="dimmed" mt="xs">
                  {t("Parameters.apOffHint")}
                </Text>
              )}
            </Tabs.Panel>

            <Tabs.Panel value="buttons" pt="md">
              <NumberInput
                mt="md"
                label={t("Parameters.buttonGpio13Track")}
                max={999}
                min={-1}
                {...form.getInputProps("button_gpio13_track")}
              />
              <Select
                mt="md"
                label={t("Parameters.buttonGpio13PullMode")}
                data={[
                  { value: "0", label: String(t("Parameters.pullModeUp")) },
                  { value: "1", label: String(t("Parameters.pullModeDown")) },
                  { value: "2", label: String(t("Parameters.pullModeNone")) },
                ]}
                value={String(form.values.button_gpio13_pull_mode ?? 0)}
                onChange={(value) =>
                  form.setFieldValue("button_gpio13_pull_mode", Number(value ?? 0))
                }
              />
              <Select
                mt="md"
                label={t("Parameters.buttonGpio13ActiveLevel")}
                data={[
                  { value: "0", label: String(t("Parameters.activeLevelLow")) },
                  { value: "1", label: String(t("Parameters.activeLevelHigh")) },
                ]}
                value={String(form.values.button_gpio13_active_level ?? 0)}
                onChange={(value) =>
                  form.setFieldValue("button_gpio13_active_level", Number(value ?? 0))
                }
              />
              <NumberInput
                mt="md"
                label={t("Parameters.buttonGpio16Track")}
                max={999}
                min={-1}
                {...form.getInputProps("button_gpio16_track")}
              />
              <Select
                mt="md"
                label={t("Parameters.buttonGpio16PullMode")}
                data={[
                  { value: "0", label: String(t("Parameters.pullModeUp")) },
                  { value: "1", label: String(t("Parameters.pullModeDown")) },
                  { value: "2", label: String(t("Parameters.pullModeNone")) },
                ]}
                value={String(form.values.button_gpio16_pull_mode ?? 0)}
                onChange={(value) =>
                  form.setFieldValue("button_gpio16_pull_mode", Number(value ?? 0))
                }
              />
              <Select
                mt="md"
                label={t("Parameters.buttonGpio16ActiveLevel")}
                data={[
                  { value: "0", label: String(t("Parameters.activeLevelLow")) },
                  { value: "1", label: String(t("Parameters.activeLevelHigh")) },
                ]}
                value={String(form.values.button_gpio16_active_level ?? 0)}
                onChange={(value) =>
                  form.setFieldValue("button_gpio16_active_level", Number(value ?? 0))
                }
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.buttonTrackHelp")}
              </Text>
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.buttonElectricalHelp")}
              </Text>
              <Group mt="md" spacing="xs">
                <Button
                  type="button"
                  variant="light"
                  loading={simulatingButton === 13}
                  onClick={() => void triggerPhysicalButton(13)}
                >
                  {t("Parameters.simulateButton13")}
                </Button>
                <Button
                  type="button"
                  variant="light"
                  loading={simulatingButton === 16}
                  onClick={() => void triggerPhysicalButton(16)}
                >
                  {t("Parameters.simulateButton16")}
                </Button>
              </Group>
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.simulateButtonHint")}
              </Text>
            </Tabs.Panel>
          </Tabs>

          <Flex justify={"space-between"} mt="md">
            <Button type="submit" fullWidth={isMobile}>
              {t("Parameters.save")}
            </Button>
          </Flex>
        </form>
      </Modal>
      <Tooltip label={t("Parameters.parameters")} withArrow>
        <ActionIcon
          onClick={open}
          variant="gradient"
          gradient={{ from: "indigo", to: "cyan", deg: 135 }}
          radius="md"
          size={44}
          aria-label={t("Parameters.parameters")}
          sx={(theme) => ({
            boxShadow: `0 8px 24px ${
              theme.colorScheme === "dark" ? "rgba(34, 139, 230, 0.45)" : "rgba(34, 139, 230, 0.3)"
            }`,
            border: `1px solid ${
              theme.colorScheme === "dark" ? "rgba(255,255,255,0.18)" : "rgba(255,255,255,0.65)"
            }`,
            transition: "transform 140ms ease, box-shadow 140ms ease",
            "&:hover": {
              transform: "translateY(-1px) scale(1.03)",
              boxShadow: `0 12px 30px ${
                theme.colorScheme === "dark" ? "rgba(34, 139, 230, 0.55)" : "rgba(34, 139, 230, 0.4)"
              }`,
            },
          })}
        >
          <IconSettings size={"1.2rem"} />
        </ActionIcon>
      </Tooltip>
    </>
  );
};

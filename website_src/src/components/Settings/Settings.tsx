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
import { useEffect, useRef, useState } from "react";
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
  const [isEditing, setIsEditing] = useState(false);
  const isApplyingRemoteRef = useRef(false);

  const getFormValuesFromData = (sourceData?: Data) => ({
    loop_file: sourceData?.loop_file,
    auto_play: sourceData?.auto_play,
    allow_play_over_playing: sourceData?.allow_play_over_playing ?? false,
    note: sourceData?.note,
    udp_port: sourceData?.udp_port,
    volume: sourceData?.volume,
    led_mode: sourceData?.led_mode ?? 0,
    led_brightness: sourceData?.led_brightness ?? 10,
    led_solid_r: sourceData?.led_solid_r ?? 0,
    led_solid_g: sourceData?.led_solid_g ?? 0,
    led_solid_b: sourceData?.led_solid_b ?? 64,
    ap_ssid: sourceData?.ap_ssid,
    ap_password: sourceData?.ap_password,
    ap_ip_config: sourceData?.ap_ip_config,
    esp_now_channel: sourceData?.esp_now_channel,
    device_mode: sourceData?.device_mode ?? 0,
    mesh_ttl: sourceData?.mesh_ttl ?? 3,
    ap_safety_timeout_s: sourceData?.ap_safety_timeout_s ?? 300,
    button_gpio13_track: sourceData?.button_gpio13_track,
    button_gpio16_track: sourceData?.button_gpio16_track,
    button_gpio13_pull_mode: sourceData?.button_gpio13_pull_mode ?? 0,
    button_gpio16_pull_mode: sourceData?.button_gpio16_pull_mode ?? 0,
    button_gpio13_active_level: sourceData?.button_gpio13_active_level ?? 0,
    button_gpio16_active_level: sourceData?.button_gpio16_active_level ?? 0,
    pcf_pull_mode: sourceData?.pcf_pull_mode ?? 0,
    pcf_active_level: sourceData?.pcf_active_level ?? 0,
    pcf_track_map:
      sourceData?.pcf_track_map && sourceData.pcf_track_map.length === 16
        ? sourceData.pcf_track_map
        : Array.from({ length: 16 }, (_, idx) => idx),
  });

  const applyDataToForm = (sourceData?: Data) => {
    isApplyingRemoteRef.current = true;
    form.setValues(getFormValuesFromData(sourceData));
    form.resetDirty();
    setIsEditing(false);
    isApplyingRemoteRef.current = false;
  };

  const form = useForm({
    initialValues: getFormValuesFromData(data),
  });

  useEffect(() => {
    if (opened) {
      return;
    }
    applyDataToForm(data);
  }, [data, opened]);

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
        onClose={() => {
          setIsEditing(false);
          close();
        }}
        title={t("Parameters.parameters")}
        centered={!isMobile}
        fullScreen={Boolean(isMobile)}
        size={isMobile ? "100%" : "lg"}
        radius="md"
      >
        <form
          onChangeCapture={() => {
            if (!isApplyingRemoteRef.current) {
              setIsEditing(true);
            }
          }}
          onSubmit={form.onSubmit(() => {
            console.log("Form Values", form.values);
            axios.post("/settings", form.values).then(() => {
              setIsEditing(false);
              fetchData();
            });
            // window.electron.ipcRenderer.send('set-settings', message)
          })}
        >
          <Tabs defaultValue="general" keepMounted={false}>
            <Tabs.List grow>
              <Tabs.Tab value="general">{t("Parameters.generalTab")}</Tabs.Tab>
              <Tabs.Tab value="network">{t("Parameters.networkTab")}</Tabs.Tab>
              <Tabs.Tab value="buttons">{t("Parameters.buttonsTab")}</Tabs.Tab>
              <Tabs.Tab value="pcf">{t("Parameters.pcfTab")}</Tabs.Tab>
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
              <Select
                mt="md"
                label={t("Parameters.ledMode")}
                data={[
                  { value: "0", label: String(t("Parameters.ledModeBattery")) },
                  { value: "1", label: String(t("Parameters.ledModeRainbow")) },
                  { value: "2", label: String(t("Parameters.ledModeOff")) },
                  { value: "3", label: String(t("Parameters.ledModeSolid")) },
                ]}
                value={String(form.values.led_mode ?? 0)}
                onChange={(value) => form.setFieldValue("led_mode", Number(value ?? 0))}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.ledBrightness")}
                max={255}
                min={0}
                {...form.getInputProps("led_brightness")}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.ledSolidR")}
                max={255}
                min={0}
                {...form.getInputProps("led_solid_r")}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.ledSolidG")}
                max={255}
                min={0}
                {...form.getInputProps("led_solid_g")}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.ledSolidB")}
                max={255}
                min={0}
                {...form.getInputProps("led_solid_b")}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.ledStatus", {
                  usb: (data?.usb_voltage ?? 0).toFixed(2),
                  battery: (data?.battery_voltage ?? 0).toFixed(2),
                  charging: data?.charge_status ? "ON" : "OFF",
                })}
              </Text>
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
                  loading={simulatingButton === 14}
                  onClick={() => void triggerPhysicalButton(14)}
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
            <Tabs.Panel value="pcf" pt="md">
              <Select
                mt="md"
                label={t("Parameters.pcfPullMode")}
                data={[
                  { value: "0", label: String(t("Parameters.pullModeUp")) },
                  { value: "1", label: String(t("Parameters.pullModeDown")) },
                ]}
                value={String(form.values.pcf_pull_mode ?? 0)}
                onChange={(value) => form.setFieldValue("pcf_pull_mode", Number(value ?? 0))}
              />
              <Select
                mt="md"
                label={t("Parameters.pcfActiveLevel")}
                data={[
                  { value: "0", label: String(t("Parameters.activeLevelLow")) },
                  { value: "1", label: String(t("Parameters.activeLevelHigh")) },
                ]}
                value={String(form.values.pcf_active_level ?? 0)}
                onChange={(value) => form.setFieldValue("pcf_active_level", Number(value ?? 0))}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.pcfMapHint")}
              </Text>
              {Array.from({ length: 16 }, (_, idx) => (
                <NumberInput
                  key={idx}
                  mt="md"
                  label={t("Parameters.pcfChannelTrack", { channel: idx })}
                  max={999}
                  min={-1}
                  value={form.values.pcf_track_map?.[idx] ?? idx}
                  onChange={(value) => {
                    const nextMap = Array.from(
                      { length: 16 },
                      (_, i) => form.values.pcf_track_map?.[i] ?? i
                    );
                    nextMap[idx] = Number(value ?? -1);
                    form.setFieldValue("pcf_track_map", nextMap);
                  }}
                />
              ))}
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
          onClick={() => {
            applyDataToForm(data);
            open();
          }}
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

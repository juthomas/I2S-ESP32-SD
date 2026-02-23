import {
  ActionIcon,
  Button,
  Flex,
  Group,
  Modal,
  NumberInput,
  Switch,
  Tabs,
  Text,
  TextInput,
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
      note: data?.note,
      udp_port: data?.udp_port,
      volume: data?.volume,
      ap_ssid: data?.ap_ssid,
      ap_password: data?.ap_password,
      ap_ip_config: data?.ap_ip_config,
      esp_now_channel: data?.esp_now_channel,
      button_gpio13_track: data?.button_gpio13_track,
      button_gpio16_track: data?.button_gpio16_track,
    },
  });

  useEffect(() => {
    form.setValues({
      loop_file: data?.loop_file,
      auto_play: data?.auto_play,
      note: data?.note,
      udp_port: data?.udp_port,
      volume: data?.volume,
      ap_ssid: data?.ap_ssid,
      ap_password: data?.ap_password,
      ap_ip_config: data?.ap_ip_config,
      esp_now_channel: data?.esp_now_channel,
      button_gpio13_track: data?.button_gpio13_track,
      button_gpio16_track: data?.button_gpio16_track,
    });
  }, [data]);

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
              <TextInput
                mt="md"
                label={t("Parameters.notes")}
                placeholder="Notes..."
                {...form.getInputProps("note")}
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
                label={t("Parameters.volume")}
                max={255}
                min={0}
                {...form.getInputProps("volume")}
              />
            </Tabs.Panel>

            <Tabs.Panel value="network" pt="md">
              <TextInput
                mt="md"
                label={t("Parameters.apSsid")}
                placeholder="I2S-SD-ABC123"
                {...form.getInputProps("ap_ssid")}
              />
              <TextInput
                mt="md"
                label={t("Parameters.apPassword")}
                placeholder="8-63 chars"
                {...form.getInputProps("ap_password")}
              />
              <TextInput
                mt="md"
                label={t("Parameters.apIpConfig")}
                placeholder="192.168.4.1"
                {...form.getInputProps("ap_ip_config")}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.espNowChannel")}
                max={13}
                min={1}
                {...form.getInputProps("esp_now_channel")}
              />
              <TextInput
                mt="md"
                label={t("Parameters.apCurrentSsid")}
                readOnly
                value={data?.ap_ssid ?? ""}
              />
              <TextInput
                mt="md"
                label={t("Parameters.apCurrentIp")}
                readOnly
                value={data?.ap_ip ?? ""}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.networkRestartHint")}
              </Text>
            </Tabs.Panel>

            <Tabs.Panel value="buttons" pt="md">
              <NumberInput
                mt="md"
                label={t("Parameters.buttonGpio13Track")}
                max={999}
                min={-1}
                {...form.getInputProps("button_gpio13_track")}
              />
              <NumberInput
                mt="md"
                label={t("Parameters.buttonGpio16Track")}
                max={999}
                min={-1}
                {...form.getInputProps("button_gpio16_track")}
              />
              <Text size="sm" c="dimmed" mt="xs">
                {t("Parameters.buttonTrackHelp")}
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
            <Button type="submit">{t("Parameters.save")}</Button>
          </Flex>
        </form>
      </Modal>
      <ActionIcon onClick={open} variant="filled" color="gray" size={"xl"}>
        <IconSettings size={"xl"} />
      </ActionIcon>
    </>
  );
};

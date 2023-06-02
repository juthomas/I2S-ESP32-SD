import {
  ActionIcon,
  Button,
  Flex,
  Modal,
  NumberInput,
  Switch,
  TextInput,
} from "@mantine/core";
import { useForm } from "@mantine/form";
import { useDisclosure } from "@mantine/hooks";
import { IconSettings } from "@tabler/icons-react";
import axios from "axios";
import { Data } from "../../App";
import { useEffect } from "react";

interface SettingsProps {
  data?: Data;
  fetchData: () => Promise<void>;
}

export const Settings = ({ data, fetchData }: SettingsProps): JSX.Element => {
  const [opened, { open, close }] = useDisclosure(false);
  const form = useForm({
    initialValues: {
      loop_file: data?.loop_file,
      auto_play: data?.auto_play,
      note: data?.note,
      udp_port: data?.udp_port,
      volume: data?.volume,
    },
  });
  useEffect(() => {
    form.setValues({
      loop_file: data?.loop_file,
      auto_play: data?.auto_play,
      note: data?.note,
      udp_port: data?.udp_port,
      volume: data?.volume,
    });
  }, [data]);

  return (
    <>
      <Modal opened={opened} onClose={close} title="Parametres" centered>
        <form
          onSubmit={form.onSubmit(() => {
            console.log("Form Values", form.values);
            const message = JSON.stringify(form.values);
            axios.post("/settings", form.values).then(() => fetchData());
            // window.electron.ipcRenderer.send('set-settings', message)
          })}
        >
          <Switch
            labelPosition="left"
            label="Loop audio"
            {...form.getInputProps("loop_file", { type: "checkbox" })}
          />
          <Switch
            mt="md"
            labelPosition="left"
            label="Lecture auto"
            {...form.getInputProps("auto_play", { type: "checkbox" })}
          />
          <TextInput
            mt="md"
            label="Notes"
            placeholder="Notes..."
            {...form.getInputProps("note")}
          />
          <NumberInput
            mt="md"
            label="Port Udp"
            max={99999}
            min={0}
            {...form.getInputProps("udp_port")}
          />
          <NumberInput
            mt="md"
            label="Volume"
            max={255}
            min={0}
            {...form.getInputProps("volume")}
          />
          <Flex justify={"space-between"} mt="md">
            <Button type="submit">Save</Button>
            {/* <ActionIcon onClick={updateValues} variant="filled" size="2.2rem">
              <IconRefresh size="1.5rem" />
            </ActionIcon> */}
          </Flex>
        </form>
      </Modal>
      <ActionIcon onClick={open} variant="filled" color="gray" size={"xl"}>
        <IconSettings size={"xl"} />
      </ActionIcon>
    </>
  );
};

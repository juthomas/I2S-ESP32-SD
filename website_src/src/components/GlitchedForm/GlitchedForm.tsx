import { FormEvent, useState } from "react";
import { GlitchedButton } from "../GlitchedButton";
import { GlitchedInput } from "../GlitchedInput";

interface GlitchedFormProps {
  label?: string;
  name?: string;
  placeholder?: string;
  value?: string;
  socket?: WebSocket | null;
}

export const GlitchedForm = ({
  label = "default",
  name = label,
  placeholder = label,
  value,
  socket = null,
}: GlitchedFormProps): JSX.Element => {
  const [inputState, setInputState] = useState(value || "");

  const submitFunction = (e: FormEvent<HTMLFormElement>) => {
    e.preventDefault();
    console.log("[DEBUG] Submit event :", inputState);
    if (socket) {
      socket.send(JSON.stringify({ settings: { [name]: inputState } }));
    }
  };

  return (
    <form onSubmit={submitFunction} style={{ color: "red" }}>
      <GlitchedInput
        inputState={inputState}
        setInputState={setInputState}
        label={label}
        placeholder={placeholder}
      />
      <GlitchedButton type="submit">Update</GlitchedButton>
    </form>
  );
};

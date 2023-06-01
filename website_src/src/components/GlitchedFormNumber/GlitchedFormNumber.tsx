import { FormEvent, useState } from "react";
import { GlitchedButton } from "../GlitchedButton";
import { GlitchedInputNumber } from "../GlitchedInputNumber/GlitchedInputNumber";

interface GlitchedFormProps {
  label?: string;
  name?: string;
  placeholder?: string;
  value?: number;
  socket?: WebSocket | null;
}

export const GlitchedFormNumber = ({
  label = "default",
  name = label,
  placeholder = label,
  value,
  socket = null,
}: GlitchedFormProps): JSX.Element => {
  const [inputState, setInputState] = useState(value || 0);

  const submitFunction = (e: FormEvent<HTMLFormElement>) => {
    e.preventDefault();
    console.log("[DEBUG] Submit event :", inputState);
    if (socket) {
      socket.send(JSON.stringify({ settings: { [name]: inputState } }));
    }
  };

  return (
    <form onSubmit={submitFunction} style={{ color: "red" }}>
      <GlitchedInputNumber
        inputState={inputState}
        setInputState={setInputState}
        label={label}
        placeholder={placeholder}
      />
      <GlitchedButton type="submit">Update</GlitchedButton>
    </form>
  );
};

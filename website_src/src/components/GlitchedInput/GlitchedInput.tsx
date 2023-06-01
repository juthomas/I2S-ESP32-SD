import './index.css'
export interface GlitchedInputProps {
  label: string;
  placeholder?: string;
  inputState: string;
  setInputState: React.Dispatch<React.SetStateAction<string>>;
}

export const GlitchedInput = ({
  label,
  placeholder,
  inputState,
  setInputState,
}: GlitchedInputProps): JSX.Element => {
  return (
    <div style={{position : 'relative'}}>
      <label>{label}</label>
      <input
      className="glitched-input"
        type="text"
        onChange={(e) => setInputState(e.target.value)}
        placeholder={placeholder}
        value={inputState}
        required
      >
      </input>
      {/* <span
        aria-hidden
        className="glitched-input-animation"
        children={inputState}
      /> */}
    </div>
  );
};

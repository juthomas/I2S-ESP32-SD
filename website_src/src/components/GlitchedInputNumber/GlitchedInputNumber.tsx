import './index.css'
export interface GlitchedInputNumberProps {
  label: string;
  placeholder?: string;
  inputState: number;
  setInputState: React.Dispatch<React.SetStateAction<number>>;
}

export const GlitchedInputNumber = ({
  label,
  placeholder,
  inputState,
  setInputState,
}: GlitchedInputNumberProps): JSX.Element => {
  return (
    <div style={{position : 'relative'}}>
      <label>{label}</label>
      <input
      className="glitched-input"
        type="number"
        onChange={(e) => setInputState(parseInt(e.target.value))}
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

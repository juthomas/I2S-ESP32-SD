import { GlitchedInput } from '../GlitchedInput';
import './index.css'
export interface GlitchedInputProps {
  // label: string;
  // placeholder?: string;
  // inputState: string;
  // setInputState: React.Dispatch<React.SetStateAction<string>>;
}

export const GlitchedCommandForm = ({

}: GlitchedInputProps): JSX.Element => {
  return (
    <div className="command ">
      <GlitchedInput label='Command' inputState='42' setInputState={(e) => { }} />
      {/* <div className="cybr-input-container">

        <label>Command</label>
        <input type="text" name="command-nu" id="command-nu" placeholder="command-nu"
          value="%command-nu%" required>
      </div> */}
      <div className="protocols">
        <div className="protocol">
          <input type="radio" id="protocol-osc-nu" name="protocol-nu" value="osc" />
          <label>OSC</label>
        </div>
        <div className="protocol">
          <input type="radio" id="protocol-udp-nu" name="protocol-nu" value="udp" />
          <label>UDP</label>
        </div>
      </div>
    </div>
  );
};

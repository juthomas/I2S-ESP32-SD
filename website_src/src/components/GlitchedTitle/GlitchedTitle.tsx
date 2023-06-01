import "./index.css";

export interface GlitchedTitleProps {
  title1?: string;
  title2?: string;
}

export const GlitchedTitle = ({
  title1 = "AS-SIMT",
  title2 = "السمت",
}: GlitchedTitleProps): JSX.Element => {
  return (
    <div style={{ position: "relative", pointerEvents: "none" }}>
      <div className="title1">
        <h1 className="glitch1 title before">{title1}</h1>
        <h1 className="glitch1 title">{title1}</h1>
        <h1 className="glitch1 title after">{title1}</h1>
      </div>
      <div className="title2">
        <h1 className="glitch2 title before">{title2}</h1>
        <h1 className="glitch2 title">{title2}</h1>
        <h1 className="glitch2 title after">{title2}</h1>
      </div>
    </div>
  );
};

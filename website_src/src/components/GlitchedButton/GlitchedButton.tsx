import "./index.css";

export type GlitchedButtonProps = React.DetailedHTMLProps<
  React.ButtonHTMLAttributes<HTMLButtonElement>,
  HTMLButtonElement
>;

export const GlitchedButton = (props: GlitchedButtonProps): JSX.Element => {
  return (
    <button {...props} className="glitched-button">
      {props.children}
      <span
        aria-hidden
        className="glitched-button-animation"
        children={props.children}
      />
    </button>
  );
};

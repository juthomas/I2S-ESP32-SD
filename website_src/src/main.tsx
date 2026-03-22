import React, { useState } from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import "./index.css";
import {
  ColorScheme,
  ColorSchemeProvider,
  MantineProvider,
} from "@mantine/core";
import { Notifications } from "@mantine/notifications";
import "./i18n";
import { useColorScheme } from "@mantine/hooks";

function Providers() {
  const preferredColorScheme = useColorScheme();
  const [colorScheme, setColorScheme] = useState<ColorScheme>(
    (localStorage.getItem("color-scheme") as ColorScheme) || preferredColorScheme
  );

  const toggleColorScheme = (value?: ColorScheme) => {
    const nextColorScheme = value || (colorScheme === "dark" ? "light" : "dark");
    setColorScheme(nextColorScheme);
    localStorage.setItem("color-scheme", nextColorScheme);
  };

  return (
    <ColorSchemeProvider
      colorScheme={colorScheme}
      toggleColorScheme={toggleColorScheme}
    >
      <MantineProvider
        withNormalizeCSS
        withGlobalStyles
        theme={{
          colorScheme,
          primaryColor: "cyan",
          primaryShade: { light: 6, dark: 5 },
          defaultRadius: "md",
          colors: {
            dark: [
              "#C9D1D9",
              "#AEB8C2",
              "#8B96A3",
              "#657183",
              "#4A5568",
              "#2D3748",
              "#1F2937",
              "#161E2E",
              "#111827",
              "#0B1220",
            ],
          },
          components: {
            Paper: {
              defaultProps: {
                radius: "md",
              },
            },
          },
        }}
      >
        <Notifications />
        <App />
      </MantineProvider>
    </ColorSchemeProvider>
  );
}

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <Providers />
  </React.StrictMode>
);

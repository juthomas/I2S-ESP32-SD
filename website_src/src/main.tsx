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

// ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
//   <React.StrictMode>
//     <ColorSchemeProvider
//       colorScheme={colorScheme}
//       toggleColorScheme={toggleColorScheme}
//     >
//       <MantineProvider withNormalizeCSS withGlobalStyles>
//         <Notifications />
//         <App />
//       </MantineProvider>
//     </ColorSchemeProvider>
//   </React.StrictMode>
// );

// import React, { useState } from 'react';
// import ReactDOM from 'react-dom';
// import App from './App';
// import './index.css';
// import { ColorSchemeProvider, MantineProvider } from '@mantine/core';
// import { Notifications } from '@mantine/notifications';
// import './i18n';

function Providers() {
  // hook will return either 'dark' or 'light' on client
  // and always 'light' during ssr as window.matchMedia is not available
  const preferredColorScheme = useColorScheme();
  const [colorScheme, setColorScheme] = useState<ColorScheme>(preferredColorScheme);
  const toggleColorScheme = (value?: ColorScheme) => {
    console.log("color scheme :", value)
    setColorScheme(value || (colorScheme === 'dark' ? 'light' : 'dark'));
  }
  console.log("color scheme 2 :", colorScheme)

  return (
    <ColorSchemeProvider
      colorScheme={colorScheme}
      toggleColorScheme={toggleColorScheme}
    >
      <MantineProvider withNormalizeCSS withGlobalStyles>
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

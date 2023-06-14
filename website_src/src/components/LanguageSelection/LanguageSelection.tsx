import { LANGUAGES } from "../../constants";
import { useTranslation } from "react-i18next";
import { Text } from "@mantine/core";
export const LanguageSelection = ({}): JSX.Element => {
  const { i18n, t } = useTranslation();
  const onChangeLang = (e: React.ChangeEvent<HTMLSelectElement>) => {
    const lang_code = e.target.value;
    i18n.changeLanguage(lang_code);
  };
  return (
    <>
      <Text> {t("title")}</Text>
      <select defaultValue={i18n.language} onChange={onChangeLang}>
        {LANGUAGES.map(({ code, label }) => (
          <option key={code} value={code}>
            {label}
          </option>
        ))}
      </select>
    </>
  );
};

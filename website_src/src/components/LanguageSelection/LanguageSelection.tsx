import { LANGUAGES } from "../../constants";
import { useTranslation } from "react-i18next";
import { Select, Text } from "@mantine/core";
import type { MantineNumberSize, MantineSize } from "@mantine/core";

interface LanguageSelectionProps {
  compact?: boolean;
  width?: MantineNumberSize;
  size?: MantineSize;
}

export const LanguageSelection = ({
  compact = false,
  width,
  size,
}: LanguageSelectionProps): JSX.Element => {
  const { i18n, t } = useTranslation();
  const currentLanguage = i18n.language.split(/-|_/)[0];
  const onChangeLang = (lang_code: string | null) => {
    if (!lang_code) return;
    i18n.changeLanguage(lang_code);
  };

  return (
    <>
      {!compact && <Text size="sm">{t("label")}</Text>}
      <Select
        w={width ?? (compact ? 120 : 180)}
        size={size ?? (compact ? "xs" : "sm")}
        value={currentLanguage}
        onChange={onChangeLang}
        data={LANGUAGES.map(({ code, label }) => ({
          value: code,
          label,
        }))}
        searchable={false}
        withinPortal={false}
        aria-label={String(t("label"))}
      />
    </>
  );
};

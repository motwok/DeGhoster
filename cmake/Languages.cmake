# Localization languages (single source of truth for the build).
# en-US is the ultimate fallback and stays embedded in the exe, so it is NOT
# listed here. Each entry: "<culture>=<LANGID hex>=<rc file>".
# Consumed by src/DeGhoster.Lang (builds resource sources) and src/DeGhoster
# (muirct split into <culture>\DeGhoster.exe.mui).
set(DEGHOSTER_LANGS
  "de-DE=0x0407=strings_de.rc"
  "fr-FR=0x040C=strings_fr.rc"
  "es-ES=0x040A=strings_es.rc"
  "it-IT=0x0410=strings_it.rc"
  "nl-NL=0x0413=strings_nl.rc"
  "pt-BR=0x0416=strings_pt_br.rc"
  "pt-PT=0x0816=strings_pt_pt.rc"
  "ru-RU=0x0419=strings_ru.rc"
  "pl-PL=0x0415=strings_pl.rc"
  "cs-CZ=0x0405=strings_cs.rc"
  "sk-SK=0x041B=strings_sk.rc"
  "hu-HU=0x040E=strings_hu.rc"
  "ro-RO=0x0418=strings_ro.rc"
  "el-GR=0x0408=strings_el.rc"
  "da-DK=0x0406=strings_da.rc"
  "fi-FI=0x040B=strings_fi.rc"
  "sv-SE=0x041D=strings_sv.rc"
  "nb-NO=0x0414=strings_nb.rc"
  "tr-TR=0x041F=strings_tr.rc"
  "uk-UA=0x0422=strings_uk.rc"
  "ja-JP=0x0411=strings_ja.rc"
  "ko-KR=0x0412=strings_ko.rc"
  "zh-CN=0x0804=strings_zh_cn.rc"
  "zh-TW=0x0404=strings_zh_tw.rc"
  "ar-SA=0x0401=strings_ar.rc"
  "he-IL=0x040D=strings_he.rc")

!ifndef SNOW_SHOT_INSTALLER_LANGUAGES_INCLUDED
!define SNOW_SHOT_INSTALLER_LANGUAGES_INCLUDED
!include "LogicLib.nsh"
!define MUI_LANGDLL_ALWAYSSHOW
!define MUI_LANGDLL_ALLLANGUAGES
!define MUI_LANGDLL_WINDOWTITLE "$(SnowShotLanguageTitle)"
!define MUI_LANGDLL_INFO "$(SnowShotLanguagePrompt)"

!macro SnowShotInstallerLanguages
  !insertmacro MUI_LANGUAGE "English"
  !insertmacro MUI_LANGUAGE "SimpChinese"
  !insertmacro MUI_LANGUAGE "TradChinese"
!macroend

; Match CPack's registry context before language selection and upgrade prompts.
!macro SnowShotLanguageContext
  Push $0
  ClearErrors
  UserInfo::GetAccountType
  Pop $0
  SetShellVarContext current
  ${If} $0 == "Admin"
  ${OrIf} $0 == "Power"
    SetShellVarContext all
  ${EndIf}
  Pop $0
!macroend

; A reinstall can change language without running the previous uninstaller.
!macro SnowShotDeleteUninstallShortcuts Folder
  Delete "${Folder}\${SnowShotUninstallShortcut1033}.lnk"
  Delete "${Folder}\${SnowShotUninstallShortcut2052}.lnk"
  Delete "${Folder}\${SnowShotUninstallShortcut1028}.lnk"
  Delete "${Folder}\Uninstall.lnk"
!macroend
!endif

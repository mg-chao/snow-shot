!ifndef SNOW_SHOT_RUNNING_APPLICATION_INCLUDED
!define SNOW_SHOT_RUNNING_APPLICATION_INCLUDED
!include "LogicLib.nsh"

!include "${__FILEDIR__}\InstallerStrings.nsh"

!ifmacrondef SnowShotConfirmClose
!macro SnowShotConfirmClose
  IfSilent declined
  MessageBox MB_YESNO|MB_ICONEXCLAMATION|MB_DEFBUTTON2 "$(SnowShotClosePrompt)" /SD IDNO IDYES closeApp
!macroend
!endif

; Windows Restart Manager identifies holders of this exact executable path.
; https://learn.microsoft.com/windows/win32/rstmgr/using-restart-manager
; The System plug-in is bundled with NSIS; no external binary plug-in is needed.
!macro SnowShotRunningApplicationFunction Prefix
Function ${Prefix}SnowShotEnsureAppClosed
  Exch $0
  Push $1
  Push $2
  Push $3
  Push $4
  Push $5
  Push $6
  IfFileExists "$0" 0 finished
  StrCpy $6 0
startSession:
  System::Call 'rstrtmgr::RmStartSession(*i .r1, i 0, w .r2) i.r2'
  StrCmp $2 0 0 failed
  System::Call '*(&w${NSIS_MAX_STRLEN} r0) p.r3'
  System::Call '*(p r3) p.r4'
  System::Call 'rstrtmgr::RmRegisterResources(i r1, i 1, p r4, i 0, p 0, i 0, p 0) i.r2'
  System::Free $4
  System::Free $3
  StrCmp $2 0 0 sessionFailed

  System::Call 'rstrtmgr::RmGetList(i r1, *i .r5, *i 0, p 0, *i .r4) i.r2'
  StrCmp $2 0 sessionFinished
  StrCmp $2 234 0 sessionFailed ; ERROR_MORE_DATA means there are file holders.
  StrCmp $6 1 sessionFailed
  !insertmacro SnowShotConfirmClose
declined:
  System::Call 'rstrtmgr::RmEndSession(i r1)'
  SetErrorLevel 10
  Quit

closeApp:
  ; RmForceShutdown first requests shutdown and terminates unresponsive apps.
  System::Call 'rstrtmgr::RmShutdown(i r1, i 1, p 0) i.r2'
  StrCmp $2 0 0 sessionFailed
  ; A session retains stopped processes for RmRestart. Use a fresh session
  ; to check for current file holders rather than treating that list as live.
  System::Call 'rstrtmgr::RmEndSession(i r1)'
  StrCpy $6 1
  Goto startSession
sessionFinished:
  System::Call 'rstrtmgr::RmEndSession(i r1)'
  Goto finished
sessionFailed:
  System::Call 'rstrtmgr::RmEndSession(i r1)'
failed:
  DetailPrint "$(SnowShotCloseFailed) ($2)"
  MessageBox MB_OK|MB_ICONSTOP "$(SnowShotCloseFailed)" /SD IDOK
  SetErrorLevel 11
  Quit
finished:
  Pop $6
  Pop $5
  Pop $4
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd
!macroend

!insertmacro SnowShotRunningApplicationFunction ""
!insertmacro SnowShotRunningApplicationFunction "un."
!endif

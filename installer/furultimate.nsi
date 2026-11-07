; FurUltimate — NSIS installer (Unicode, Modern UI 2)
Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

!ifndef VERSION
  !define VERSION "1.2.0"
!endif
!ifndef STAGE
  !define STAGE "..\dist\stage"
!endif
!ifndef OUTFILE
  !define OUTFILE "..\dist\FurUltimate-${VERSION}-Setup.exe"
!endif

!define APPNAME "FurUltimate"
!define COMPANY "FurUltimate"
!define EXE "FurUltimate.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\FurUltimate"

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\FurUltimate"
InstallDirRegKey HKLM "Software\FurUltimate" "InstallDir"
RequestExecutionLevel admin
BrandingText "FurUltimate ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1055 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=1055 "CompanyName" "${COMPANY}"
VIAddVersionKey /LANG=1055 "FileDescription" "FurUltimate Kurulum"
VIAddVersionKey /LANG=1055 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1055 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1055 "LegalCopyright" "Copyright (C) 2026 FurUltimate"

!include "MUI2.nsh"
!include "x64.nsh"

!define MUI_ICON "..\assets\app.ico"
!define MUI_UNICON "..\assets\app.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\docs\USER_GUIDE.md"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Turkish"
!insertmacro MUI_LANGUAGE "English"

LangString SecMain ${LANG_TURKISH} "FurUltimate (gerekli)"
LangString SecMain ${LANG_ENGLISH} "FurUltimate (required)"
LangString SecDesk ${LANG_TURKISH} "Masaüstü kısayolu"
LangString SecDesk ${LANG_ENGLISH} "Desktop shortcut"
LangString NeedX64 ${LANG_TURKISH} "FurUltimate 64-bit Windows 10 veya 11 gerektirir."
LangString NeedX64 ${LANG_ENGLISH} "FurUltimate requires 64-bit Windows 10 or 11."
LangString AskData ${LANG_TURKISH} "Kullanıcı verileri (sonuç geçmişi, ayarlar, günlükler, ekran görüntüleri) da silinsin mi?"
LangString AskData ${LANG_ENGLISH} "Also delete user data (result history, settings, logs, screenshots)?"
LangString Running ${LANG_TURKISH} "FurUltimate çalışıyor. Devam etmeden önce uygulamayı kapatın."
LangString Running ${LANG_ENGLISH} "FurUltimate is running. Close the application before continuing."

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "$(NeedX64)"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

Function CheckRunning
  FindWindow $0 "FurUltimateMain" ""
  StrCmp $0 0 +3
    MessageBox MB_ICONEXCLAMATION|MB_RETRYCANCEL "$(Running)" IDRETRY -2
    Abort
FunctionEnd

Section "$(SecMain)" SEC_MAIN
  SectionIn RO
  Call CheckRunning
  SetOutPath "$INSTDIR"
  File "${STAGE}\${EXE}"
  File "${STAGE}\README.md"
  File "${STAGE}\RELEASE_NOTES.md"
  File "${STAGE}\LICENSE.txt"
  File "${STAGE}\THIRD_PARTY_NOTICES.txt"
  SetOutPath "$INSTDIR\docs"
  File "${STAGE}\docs\*.md"
  SetOutPath "$INSTDIR"
  File /nonfatal "${STAGE}\leaderboard.json"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\FurUltimate"
  CreateShortcut "$SMPROGRAMS\FurUltimate\FurUltimate.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0

  WriteRegStr HKLM "Software\FurUltimate" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${COMPANY}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE},0"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  SectionGetSize ${SEC_MAIN} $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section "$(SecDesk)" SEC_DESK
  CreateShortcut "$DESKTOP\FurUltimate.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
SectionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
  !insertmacro MUI_UNGETLANGUAGE
FunctionEnd

Section "Uninstall"
  FindWindow $0 "FurUltimateMain" ""
  StrCmp $0 0 +3
    MessageBox MB_ICONEXCLAMATION "$(Running)"
    Abort
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\RELEASE_NOTES.md"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.txt"
  Delete "$INSTDIR\leaderboard.json"
  RMDir /r "$INSTDIR\docs"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$DESKTOP\FurUltimate.lnk"
  RMDir /r "$SMPROGRAMS\FurUltimate"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\FurUltimate"
  IfSilent done
  MessageBox MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION "$(AskData)" IDNO done
    SetShellVarContext current
    RMDir /r "$LOCALAPPDATA\FurUltimate"
  done:
SectionEnd

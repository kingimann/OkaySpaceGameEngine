; OkaySpace Windows installer (NSIS / Modern UI 2).
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef DISTDIR
  !define DISTDIR "build-win-dist"
!endif
!ifndef OUTFILE
  !define OUTFILE "OkaySpace-Setup.exe"
!endif

Name "OkaySpace ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\OkaySpace"
InstallDirRegKey HKCU "Software\OkaySpace" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "OkaySpace ${VERSION}"

Var IsUpdate                 ; 1 when an existing OkaySpace install was found

; Detect a prior install up front: if OkaySpace is already on this system, reuse
; its folder and UPDATE it in place instead of offering a second copy somewhere
; new. The directory page is then skipped (see SkipDirIfUpdate). Looked for, in
; order: where the last installer put it, the Apps & features entry, the folder
; this setup runs from (an unzipped copy has no registry entry), and the default.
Function .onInit
  ReadRegStr $0 HKCU "Software\OkaySpace" "InstallDir"
  Call UseIfInstalled
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OkaySpace" "InstallLocation"
  Call UseIfInstalled
  StrCpy $0 "$EXEDIR"
  Call UseIfInstalled
  StrCpy $0 "$LOCALAPPDATA\OkaySpace"
  Call UseIfInstalled
FunctionEnd

; $0 = a candidate folder; the first one holding OkaySpace.exe wins.
Function UseIfInstalled
  ${If} $IsUpdate != "1"
  ${AndIf} $0 != ""
  ${AndIf} ${FileExists} "$0\OkaySpace.exe"
    StrCpy $INSTDIR $0
    StrCpy $IsUpdate "1"
  ${EndIf}
FunctionEnd

; $0 = a program in $INSTDIR. A running exe can't be opened for writing, so ask
; the user to close OkaySpace instead of failing halfway through the copy.
Function WaitUntilClosed
  ${IfNot} ${FileExists} "$INSTDIR\$0"
    Return
  ${EndIf}
  retry:
    ClearErrors
    FileOpen $1 "$INSTDIR\$0" a
    ${If} ${Errors}
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OkaySpace is still running ($0).$\r$\n$\r$\nClose it, then click Retry." IDRETRY retry
      Abort
    ${EndIf}
    FileClose $1
FunctionEnd

Function SkipDirIfUpdate
  ${If} $IsUpdate == "1"
    Abort                    ; already installed -> don't ask where, just update it
  ${EndIf}
FunctionEnd

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\OkaySpace.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch OkaySpace now"
!define MUI_WELCOMEPAGE_TITLE "Welcome to the OkaySpace setup"
!define MUI_WELCOMEPAGE_TEXT "This installs or updates OkaySpace ${VERSION}.$\r$\n$\r$\nIf OkaySpace is already installed, it is UPDATED in place — your games, projects and saves (in your Documents folder) are never touched. (The app also updates itself automatically from inside OkaySpace.)"

!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipDirIfUpdate
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "OkaySpace" SecMain
  ${If} $IsUpdate == "1"
    DetailPrint "Updating OkaySpace in $INSTDIR"
  ${EndIf}
  StrCpy $0 "OkaySpace.exe"
  Call WaitUntilClosed
  StrCpy $0 "Tools\OkayEngine.exe"
  Call WaitUntilClosed
  StrCpy $0 "Tools\OkaySpacePlayer.exe"
  Call WaitUntilClosed

  SetOutPath "$INSTDIR"
  ; Forward slash + "*" so this resolves on the build host whether makensis runs
  ; on Windows or (as in CI) on Linux, where "\" is literal and "*.*" would skip
  ; extensionless files.
  File /r "${DISTDIR}/*"

  CreateShortcut "$SMPROGRAMS\OkaySpace.lnk" "$INSTDIR\OkaySpace.exe" "" "$INSTDIR\OkaySpace.exe" 0
  CreateShortcut "$DESKTOP\OkaySpace.lnk"    "$INSTDIR\OkaySpace.exe" "" "$INSTDIR\OkaySpace.exe" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\OkaySpace" "InstallDir" "$INSTDIR"
  !define ARP "Software\Microsoft\Windows\CurrentVersion\Uninstall\OkaySpace"
  WriteRegStr   HKCU "${ARP}" "DisplayName"     "OkaySpace"
  WriteRegStr   HKCU "${ARP}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKCU "${ARP}" "Publisher"       "OkaySpace"
  WriteRegStr   HKCU "${ARP}" "DisplayIcon"     "$INSTDIR\OkaySpace.exe"
  WriteRegStr   HKCU "${ARP}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKCU "${ARP}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegDWORD HKCU "${ARP}" "NoModify" 1
  WriteRegDWORD HKCU "${ARP}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\OkaySpace.lnk"
  Delete "$DESKTOP\OkaySpace.lnk"
  ; Remove what setup installed (and the launcher's own settings and official
  ; game), never the whole folder: the launcher and the editor create projects
  ; in Projects\ and Tools\Projects\ by default, and community\ / downloads\
  ; hold the user's files. The folders go only once they're empty.
  Delete "$INSTDIR\OkaySpace.exe"
  Delete "$INSTDIR\OkaySpace.exe.old"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\steam_api64.dll"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\VERSION.txt"
  Delete "$INSTDIR\launcher.cfg"
  Delete "$INSTDIR\notifications.cfg"
  Delete "$INSTDIR\news.cfg"
  Delete "$INSTDIR\scheduled.cfg"
  Delete "$INSTDIR\servers.cfg"
  Delete "$INSTDIR\chat_seen_*.cfg"
  Delete "$INSTDIR\Tools\OkayEngine.exe"
  Delete "$INSTDIR\Tools\OkaySpacePlayer.exe"
  Delete "$INSTDIR\Tools\okayspace-relay.exe"
  Delete "$INSTDIR\Tools\okayui_d3d11_demo.exe"
  Delete "$INSTDIR\Tools\SDL2.dll"
  Delete "$INSTDIR\Tools\steam_api64.dll"
  Delete "$INSTDIR\Tools\VERSION.txt"
  Delete "$INSTDIR\Tools\accounts.md"
  RMDir "$INSTDIR\Tools"
  RMDir /r "$INSTDIR\Assets\Textures"
  RMDir /r "$INSTDIR\Assets\Fonts"
  RMDir "$INSTDIR\Assets"
  RMDir /r "$INSTDIR\Games\Official"
  RMDir "$INSTDIR\Games"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OkaySpace"
  DeleteRegKey HKCU "Software\OkaySpace"
SectionEnd

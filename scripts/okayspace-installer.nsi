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
; new. The directory page is then skipped (see SkipDirIfUpdate).
Function .onInit
  ReadRegStr $0 HKCU "Software\OkaySpace" "InstallDir"
  ${If} $0 != ""
    ${AndIf} ${FileExists} "$0\OkaySpace.exe"
      StrCpy $INSTDIR $0
      StrCpy $IsUpdate "1"
  ${EndIf}
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
  SetOutPath "$INSTDIR"
  File /r "${DISTDIR}\*.*"

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
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\OkaySpace"
  DeleteRegKey HKCU "Software\OkaySpace"
SectionEnd

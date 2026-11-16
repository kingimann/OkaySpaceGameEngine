; OkaySpace Windows installer (NSIS / Modern UI 2). One person, no administrator rights.
;
; What it does and why, in plain words (the long version is docs/installer.md; the audit it comes from is docs/install_audit.md):
;   * Before it changes anything it checks the folder (can be made and written), the free space, and that NO program of
;     the distribution is running. In silent mode (/S) a failed check ends Setup with a non-zero exit code and the reason is
;     in %TEMP%\OkaySpace-setup.log. Exit codes: 0 ok, 1 cancelled, 10 a program is running, 11 folder cannot be used,
;     12 not enough free space, 13 a newer version is installed (use /FORCE), 14 the files could not be unpacked or did not
;     check out, 15 the new files could not be put in place (the old ones were put back), 16 another Setup is running,
;     17 (uninstaller) Uninstall.exe is not in an OkaySpace folder.
;   * An update is staged: the new files are unpacked beside the old ones, every file is checked against the size in the
;     generated list, then each old file is renamed to <name>.old and the new one moved in. Any error puts everything back.
;     A full disk therefore cannot leave a half-written folder.
;   * The Apps & features entry, the shortcuts and the uninstaller are written only after the files are in place.
;   * The uninstaller (a page with one box, default off) removes what Setup installed (from the generated list), the editor
;     versions, logs, leftovers, temp files and the sign-in token, keeps projects and settings unless the box is ticked, and
;     lists what it kept. It never removes a file Setup did not install, except what the programs themselves made.
;
; Written from the NSIS instruction reference and docs/install_audit.md. No code from other installer scripts. Only what is in the
; standard NSIS package is used (MUI2, LogicLib, FileFunc, WordFunc, WinVer, nsDialogs and the System plugin).
;
; makensis -DVERSION=<x.y.z> -DDISTDIR=<absolute path of build-win-dist> -DOUTFILE=<OkaySpace-Setup.exe> scripts/okayspace-installer.nsi
;   -DFILELIST=<file>      use this generated list instead of running scripts/gen-installer-filelist.py (python3 is needed otherwise)
;   -DSPACE_MARGIN_KB=<n>  free space asked for on top of the files (default 16384); the test lowers it
Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WordFunc.nsh"
!include "WinVer.nsh"
!include "nsDialogs.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef DISTDIR
  !define DISTDIR "build-win-dist"
!endif
!ifndef OUTFILE
  !define OUTFILE "OkaySpace-Setup.exe"
!endif
!ifndef SPACE_MARGIN_KB
  !define SPACE_MARGIN_KB 16384
!endif

; ---- the list of files Setup installs, made from the distribution folder -----------------------------------------------------
; EACH_DIST_FILE / EACH_DIST_PROGRAM / EACH_DIST_DIR (see scripts/gen-installer-filelist.py). It is made now, from the same
; folder the File command below packs, so the two always agree (including programs that were signed after the build).
!ifdef FILELIST
  !include "${FILELIST}"
!else
  !tempfile FILELIST_GENERATED
  !echo "Making the installer file list from ${DISTDIR}"
  ; makensis runs with the script's own folder as the current folder, so the generator is found by its plain name.
  !system 'python3 "gen-installer-filelist.py" "${DISTDIR}" "${FILELIST_GENERATED}"' = 0
  !include "${FILELIST_GENERATED}"
  !delfile "${FILELIST_GENERATED}"
!endif

Name "OkaySpace ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\OkaySpace"
InstallDirRegKey HKCU "Software\OkaySpace" "InstallDir"
RequestExecutionLevel user
BrandingText "OkaySpace ${VERSION}"
ShowUninstDetails show

!define APP_KEY       "Software\OkaySpace"
!define ARP_KEY       "Software\Microsoft\Windows\CurrentVersion\Uninstall\OkaySpace"
!define STAGE_NAME    ".okayspace-setup-new"
!define MANIFEST_NAME "installed-files.txt"
!define SETUP_LOG     "OkaySpace-setup.log"
!define UNINST_LOG    "OkaySpace-uninstall.log"
!define MUTEX_NAME    "Local\OkaySpaceSetupAndUninstall"

; Exit codes (also in the header above and docs/installer.md).
!define EXIT_RUNNING 10
!define EXIT_FOLDER  11
!define EXIT_SPACE   12
!define EXIT_NEWER   13
!define EXIT_UNPACK  14
!define EXIT_SWAP    15
!define EXIT_BUSY    16
!define EXIT_NOTOURS 17            ; the uninstaller was started from a folder that is not an OkaySpace install

!insertmacro GetParameters
!insertmacro GetOptions
!insertmacro GetParent
!insertmacro DriveSpace
!insertmacro GetTime
!insertmacro VersionCompare
!insertmacro un.GetParameters
!insertmacro un.GetOptions
!insertmacro un.GetTime

; State lives in variables. The registers $0-$9 and $R0-$R9 are scratch: any Call may change them (the log function keeps them).
Var IsUpdate        ; "1" when an existing OkaySpace install was found
Var OldVersion      ; the installed version ("" = none or unknown)
Var ForceFlag       ; "1" with /FORCE
Var LogFile         ; the log file ("" = no log)
Var StageDir        ; $INSTDIR\.okayspace-setup-new
Var FolderError     ; why the folder cannot be used ("" = fine)
Var FolderCode      ; the exit code for it
Var WarnText        ; warnings about the folder, one per line
Var RunningList     ; the programs that are running, "a, b"
Var VerifyError     ; first file that did not check out
Var VerifyCount
Var SwapError       ; first error while putting files in place
Var SwapCount       ; how many files were swapped in
Var RbLimit         ; roll back this many files (-1 = every file that has a .old)
Var RbIndex
Var RbCount         ; how many files were put back
Var SwapStarted     ; "1" once the first old file was moved aside
Var CleanedUp
Var FailCode
Var WarnShown
Var FailQuiet       ; "1" = the person just answered a question, so no second message box
Var Quiet           ; "1" while the install page should show only the plain progress lines (SetDetailsPrint none; plugin calls switch it back on)
Var CreatedAbove    ; the nearest folder that already existed when Setup made the install folder ("" = Setup made nothing)
; uninstaller
Var UnRemoveData    ; "1" = also remove projects, settings and sign-in
Var UnCheck         ; the check box
Var UnKept          ; the "Kept:" lines for the last page (cut at about 700 characters; the log has them all)
Var UnKeptMore      ; "1" when lines were left out of UnKept (they are in the log)
Var UnSweepProjects ; "1" = the sweep keeps folders that hold a project
Var UnGone          ; number of files removed
Var UnLocal         ; %LOCALAPPDATA%\OkaySpace

; ---- the log --------------------------------------------------------------------------------------------------------------------
; UTF-16 with a byte order mark (so a folder name with any letters is written right; Notepad opens it).
!macro DEFINE_LOG UN
  Function ${UN}LogStart
    ClearErrors
    FileOpen $R0 "$LogFile" w
    ${If} ${Errors}
      StrCpy $LogFile ""
    ${Else}
      FileWriteWord $R0 65279
      FileClose $R0
    ${EndIf}
  FunctionEnd

  ; Push the text, then Call. Keeps the registers.
  Function ${UN}LogWrite
    Exch $R0
    Push $R1
    Push $R2
    Push $R3
    Push $R4
    Push $R5
    Push $R6
    Push $R7
    Push $R8
    ${If} $LogFile != ""
      ${${UN}GetTime} "" "L" $R1 $R2 $R3 $R4 $R5 $R6 $R7
      ClearErrors
      FileOpen $R8 "$LogFile" a
      ${IfNot} ${Errors}
        FileSeek $R8 0 END
        FileWriteUTF16LE $R8 "$R3-$R2-$R1 $R5:$R6:$R7  $R0$\r$\n"
        FileClose $R8
      ${EndIf}
    ${EndIf}
    ; Any call into the System plugin (GetTime uses it) switches the details display back on; keep it off while it should be off.
    ${If} $Quiet == "1"
      SetDetailsPrint none
    ${EndIf}
    Pop $R8
    Pop $R7
    Pop $R6
    Pop $R5
    Pop $R4
    Pop $R3
    Pop $R2
    Pop $R1
    Pop $R0
  FunctionEnd
!macroend
!insertmacro DEFINE_LOG ""
!insertmacro DEFINE_LOG "un."

!macro LogMsg _TEXT
  Push "${_TEXT}"
  Call LogWrite
!macroend
!define Log "!insertmacro LogMsg"
!macro un.LogMsg _TEXT
  Push "${_TEXT}"
  Call un.LogWrite
!macroend
!define LogU "!insertmacro un.LogMsg"

; Push the code, then the message, then Call. Writes the log, shows the reason (not in silent mode), undoes what was started, and
; ends Setup with the code.
!macro Die _CODE _TEXT
  Push ${_CODE}
  Push "${_TEXT}"
  Call Fail
!macroend

; ---- small helpers ---------------------------------------------------------------------------------------------------------------
; Push a path, Call, Pop the size in bytes (-1 when it cannot be opened).
Function FileSize
  Exch $R0
  Push $R1
  ClearErrors
  FileOpen $R1 "$R0" r
  ${If} ${Errors}
    StrCpy $R0 -1
  ${Else}
    FileSeek $R1 0 END $R0
    FileClose $R1
  ${EndIf}
  Pop $R1
  Exch $R0
FunctionEnd

; (Contains, CleanLine and SafeStalePath are defined once for the installer and once for the uninstaller: see DEFINE_LIST_HELPERS.)

; Push a version text ("2.876.0"), Call, Pop "1" when it is only digits and dots (and not empty, not too long).
Function ValidVersion
  Exch $R0
  Push $R1
  Push $R2
  Push $R3
  StrLen $R1 $R0
  ${If} $R1 = 0
  ${OrIf} $R1 > 32
    StrCpy $R0 0
    Goto done
  ${EndIf}
  StrCpy $R2 0
  loop:
    StrCpy $R3 $R0 1 $R2
    ${If} $R3 == "."
    ${OrIf} $R3 == "0"
    ${OrIf} $R3 == "1"
    ${OrIf} $R3 == "2"
    ${OrIf} $R3 == "3"
    ${OrIf} $R3 == "4"
    ${OrIf} $R3 == "5"
    ${OrIf} $R3 == "6"
    ${OrIf} $R3 == "7"
    ${OrIf} $R3 == "8"
    ${OrIf} $R3 == "9"
      IntOp $R2 $R2 + 1
      ${If} $R2 < $R1
        Goto loop
      ${EndIf}
      StrCpy $R0 1
    ${Else}
      StrCpy $R0 0
    ${EndIf}
  done:
  Pop $R3
  Pop $R2
  Pop $R1
  Exch $R0
FunctionEnd

; Push a file name, Call, Pop the first line of it without the line end and spaces ("" when it cannot be read).
Function FirstLine
  Exch $R0
  Push $R1
  Push $R2
  ClearErrors
  FileOpen $R1 "$R0" r
  ${If} ${Errors}
    StrCpy $R0 ""
  ${Else}
    FileRead $R1 $R0
    FileClose $R1
    trim:
      StrCpy $R2 $R0 1 -1
      ${If} $R2 == "$\r"
      ${OrIf} $R2 == "$\n"
      ${OrIf} $R2 == " "
        StrCpy $R0 $R0 -1
        Goto trim
      ${EndIf}
  ${EndIf}
  Pop $R2
  Pop $R1
  Exch $R0
FunctionEnd

; Push the text to search in, Push the text to look for, Call, Pop the position of the first match (0 is the start) or -1.
; Capital letters do not matter.
Function IndexOf
  Exch $R1
  Exch
  Exch $R0
  Push $R2
  Push $R3
  Push $R4
  Push $R5
  StrLen $R2 $R0
  StrLen $R3 $R1
  StrCpy $R4 0
  ${If} $R3 = 0
    StrCpy $R0 -1
    Goto done
  ${EndIf}
  loop:
    IntOp $R5 $R4 + $R3
    IntCmp $R5 $R2 look look gone
  look:
    StrCpy $R5 $R0 $R3 $R4
    StrCmp $R5 $R1 yes
    IntOp $R4 $R4 + 1
    Goto loop
  yes:
    StrCpy $R0 $R4
    Goto done
  gone:
    StrCpy $R0 -1
  done:
  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R2
  Exch $R0
  Exch
  Pop $R1
FunctionEnd

; The folder after /D= on the real command line (NSIS takes /D= out of its own copy, and quietly puts the default folder in its
; place when it cannot use the one given, so an unusable folder would never be noticed). Out: $R0, "" when there is none.
Function ReadDirSwitch
  System::Call 'kernel32::GetCommandLineW() w .R1'
  Push $R1
  Push " /D="
  Call IndexOf
  Pop $R2
  ${If} $R2 < 0
    StrCpy $R0 ""
    Return
  ${EndIf}
  IntOp $R2 $R2 + 4
  StrCpy $R0 $R1 "" $R2
  trim:
    StrCpy $R3 $R0 1 -1
    ${If} $R3 == " "
    ${OrIf} $R3 == $\"
      StrCpy $R0 $R0 -1
      Goto trim
    ${EndIf}
  StrCpy $R3 $R0 1
  ${If} $R3 == $\"
    StrCpy $R0 $R0 "" 1
  ${EndIf}
FunctionEnd

; Small helpers on text, defined for the installer (UN = "") and for the uninstaller (UN = "un."), so there is one copy of the code.
!macro DEFINE_LIST_HELPERS UN
  ; Push the text to search in, Push the text to look for, Call, Pop "1" (found) or "0". Capital letters do not matter.
  Function ${UN}Contains
    Exch $R1
    Exch
    Exch $R0
    Push $R2
    Push $R3
    Push $R4
    Push $R5
    StrLen $R2 $R0
    StrLen $R3 $R1
    StrCpy $R4 0
    ${If} $R3 = 0
      StrCpy $R0 0
      Goto done
    ${EndIf}
    loop:
      IntOp $R5 $R4 + $R3
      IntCmp $R5 $R2 look look gone
    look:
      StrCpy $R5 $R0 $R3 $R4
      StrCmp $R5 $R1 yes
      IntOp $R4 $R4 + 1
      Goto loop
    yes:
      StrCpy $R0 1
      Goto done
    gone:
      StrCpy $R0 0
    done:
    Pop $R5
    Pop $R4
    Pop $R3
    Pop $R2
    Exch $R0
    Exch
    Pop $R1
  FunctionEnd

  ; Push a line read from a list, Call, Pop it without line end and spaces. Lines that start with # are empty.
  Function ${UN}CleanLine
    Exch $R0
    Push $R1
    trim:
      StrCpy $R1 $R0 1 -1
      ${If} $R1 == "$\r"
      ${OrIf} $R1 == "$\n"
      ${OrIf} $R1 == " "
        StrCpy $R0 $R0 -1
        Goto trim
      ${EndIf}
    StrCpy $R1 $R0 1
    ${If} $R1 == "#"
      StrCpy $R0 ""
    ${EndIf}
    Pop $R1
    Exch $R0
  FunctionEnd


  ; Push a path from the list, Call, Pop "1" when it is safe to remove: inside the install folder, no wildcards, not in a folder that
  ; holds the person's work. (The list is a plain file; do not trust it blindly.)
  Function ${UN}SafeStalePath
    Exch $R0
    Push $R1
    Push $R2
    StrLen $R1 $R0
    ${If} $R1 = 0
    ${OrIf} $R1 > 200
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R2 $R0 1
    ${If} $R2 == "\"
    ${OrIf} $R2 == "/"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    ; no "..", no drive letter or stream, no wildcard, no forward slash
    Push $R0
    Push ".."
    Call ${UN}Contains
    Pop $R2
    ${If} $R2 == 1
      StrCpy $R0 0
      Goto done
    ${EndIf}
    Push $R0
    Push ":"
    Call ${UN}Contains
    Pop $R2
    ${If} $R2 == 1
      StrCpy $R0 0
      Goto done
    ${EndIf}
    Push $R0
    Push "*"
    Call ${UN}Contains
    Pop $R2
    ${If} $R2 == 1
      StrCpy $R0 0
      Goto done
    ${EndIf}
    Push $R0
    Push "?"
    Call ${UN}Contains
    Pop $R2
    ${If} $R2 == 1
      StrCpy $R0 0
      Goto done
    ${EndIf}
    Push $R0
    Push "/"
    Call ${UN}Contains
    Pop $R2
    ${If} $R2 == 1
      StrCpy $R0 0
      Goto done
    ${EndIf}
    ; the folders that hold the person's work are never touched
    StrCpy $R2 $R0 9
    ${If} $R2 == "Projects\"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R2 $R0 9
    ${If} $R2 == "Versions\"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R2 $R0 6
    ${If} $R2 == "Games\"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R2 $R0 10
    ${If} $R2 == "community\"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R2 $R0 10
    ${If} $R2 == "downloads\"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R2 $R0 8
    ${If} $R2 == "exports\"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R2 $R0 15
    ${If} $R2 == "Tools\Projects\"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R2 $R0 5
    ${If} $R2 == ".okay"
      StrCpy $R0 0
      Goto done
    ${EndIf}
    StrCpy $R0 1
    done:
    Pop $R2
    Pop $R1
    Exch $R0
  FunctionEnd


!macroend
!insertmacro DEFINE_LIST_HELPERS ""
!insertmacro DEFINE_LIST_HELPERS "un."

; ---- finding an earlier install -----------------------------------------------------------------------------------------------
; Detect a prior install up front: if OkaySpace is already on this system, reuse its folder and UPDATE it in place instead of
; offering a second copy somewhere new. The directory page is then skipped. Looked for, in order: where the last installer put it,
; the Apps & features entry, the folder this setup runs from (an unzipped copy has no registry entry), and the default.
Function .onInit
  StrCpy $LogFile "$TEMP\${SETUP_LOG}"
  Call LogStart
  ${Log} "Setup started: OkaySpace ${VERSION}"

  ${GetParameters} $R0
  ${Log} "Command line: $R0"
  ${GetOptions} $R0 "/FORCE" $R1
  ${IfNot} ${Errors}
    StrCpy $ForceFlag "1"
  ${EndIf}
  ${WinVerGetMajor} $R1
  ${WinVerGetMinor} $R2
  ${WinVerGetBuild} $R3
  ${Log} "Windows $R1.$R2 build $R3"

  ; Only one Setup or uninstall at a time (they would both change the same folder).
  System::Call 'kernel32::CreateMutexW(i 0, i 0, w "${MUTEX_NAME}") i .r1 ?e'
  Pop $R0
  ${If} $R0 = 183
    ${Log} "FAILED: another OkaySpace Setup or uninstall is already running"
    MessageBox MB_OK|MB_ICONSTOP "Another OkaySpace Setup or uninstall is already running.$\r$\n$\r$\nWait for it to finish, then run Setup again." /SD IDOK
    ${Log} "Exit code ${EXIT_BUSY}."
    SetErrorLevel ${EXIT_BUSY}
    Abort
  ${EndIf}

  ReadRegStr $0 HKCU "${APP_KEY}" "InstallDir"
  Call UseIfInstalled
  ReadRegStr $0 HKCU "${ARP_KEY}" "InstallLocation"
  Call UseIfInstalled
  StrCpy $0 "$EXEDIR"
  Call UseIfInstalled
  StrCpy $0 "$LOCALAPPDATA\OkaySpace"
  Call UseIfInstalled

  ; /D=<folder> is for a new install. With an install already there, that one is updated (a second copy would leave two installs
  ; and one Apps & features entry); the log says so.
  Call ReadDirSwitch
  ${If} $R0 != ""
    ${If} $IsUpdate == "1"
      ${If} $R0 != $INSTDIR
        ${Log} "WARNING: /D= asked for $R0, but OkaySpace is already installed in $INSTDIR, so that install is updated."
      ${EndIf}
    ${Else}
      StrCpy $INSTDIR $R0
      ${Log} "The folder was chosen with /D=: $R0"
    ${EndIf}
  ${EndIf}
  Call TrimInstDir
  ${If} $IsUpdate == "1"
    Push "$INSTDIR\VERSION.txt"
    Call FirstLine
    Pop $OldVersion
    Push $OldVersion
    Call ValidVersion
    Pop $R0
    ${If} $R0 != 1
      ReadRegStr $OldVersion HKCU "${ARP_KEY}" "DisplayVersion"
      Push $OldVersion
      Call ValidVersion
      Pop $R0
      ${If} $R0 != 1
        StrCpy $OldVersion ""
      ${EndIf}
    ${EndIf}
    ${Log} "Found an install in $INSTDIR, version '$OldVersion': this is an update"
  ${Else}
    ${Log} "No earlier install found: this is a new install into $INSTDIR"
  ${EndIf}
FunctionEnd

; $0 = a candidate folder; the first one holding an install (OkaySpace.exe AND Tools\OkayEngine.exe) wins. OkaySpace.exe alone isn't
; enough: the release also offers the launcher as a single download, and with it beside this setup in Downloads, setup would
; quietly install into Downloads.
Function UseIfInstalled
  ${If} $IsUpdate != "1"
  ${AndIf} $0 != ""
  ${AndIf} ${FileExists} "$0\OkaySpace.exe"
  ${AndIf} ${FileExists} "$0\Tools\OkayEngine.exe"
    StrCpy $INSTDIR $0
    StrCpy $IsUpdate "1"
  ${EndIf}
FunctionEnd

; No trailing backslash on the install folder (but keep "C:\").
Function TrimInstDir
  trim:
    StrLen $R0 $INSTDIR
    ${If} $R0 > 3
      StrCpy $R1 $INSTDIR 1 -1
      ${If} $R1 == "\"
        StrCpy $INSTDIR $INSTDIR -1
        Goto trim
      ${EndIf}
    ${EndIf}
FunctionEnd

; Called by Modern UI when the person confirms the cancel question (see MUI_CUSTOMFUNCTION_ABORT below).
Function OnSetupAbort
  ${Log} "Setup was cancelled by the person."
FunctionEnd

; ---- checks before anything is changed ----------------------------------------------------------------------------------------
; Looks at $INSTDIR without changing anything (a probe file is written and removed). Sets $FolderError / $FolderCode when the folder
; cannot be used, and puts warnings in $WarnText. $R9 = "1" also makes the folder, and probes inside it.
Function CheckFolder
  StrCpy $FolderError ""
  StrCpy $FolderCode 0
  StrCpy $WarnText ""
  StrCpy $WarnShown ""

  StrLen $R0 $INSTDIR
  ${If} $R0 < 4
    StrCpy $FolderError "Choose a folder for OkaySpace, not a whole drive."
    StrCpy $FolderCode ${EXIT_FOLDER}
    Return
  ${EndIf}
  StrCpy $R0 $INSTDIR 1 1
  StrCpy $R1 $INSTDIR 2
  ${If} $R0 != ":"
  ${AndIf} $R1 != "\\"
    StrCpy $FolderError "$INSTDIR is not a full folder name. Use one that starts with a drive letter, such as C:\OkaySpace."
    StrCpy $FolderCode ${EXIT_FOLDER}
    Return
  ${EndIf}

  ; Find the nearest folder that exists. A file in the way is an error.
  StrCpy $R1 $INSTDIR
  StrCpy $R3 0
  up:
    IntOp $R3 $R3 + 1
    ${If} $R3 > 60
      StrCpy $FolderError "The folder $INSTDIR is not on a drive Setup can use."
      StrCpy $FolderCode ${EXIT_FOLDER}
      Return
    ${EndIf}
    ${If} ${FileExists} "$R1\*.*"
      Goto found
    ${EndIf}
    ${If} ${FileExists} "$R1"
      StrCpy $FolderError "A file named $R1 is in the way, so the folder $INSTDIR cannot be made."
      StrCpy $FolderCode ${EXIT_FOLDER}
      Return
    ${EndIf}
    ${GetParent} "$R1" $R2
    ${If} $R2 == ""
    ${OrIf} $R2 == $R1
      StrCpy $FolderError "The folder $INSTDIR is not on a drive Setup can use."
      StrCpy $FolderCode ${EXIT_FOLDER}
      Return
    ${EndIf}
    StrCpy $R1 $R2
    Goto up
  found:
  ; Really make the folder when asked to (the person pressed Install, or this is a silent run).
  ${If} $R9 == "1"
    ${IfNot} ${FileExists} "$INSTDIR\*.*"
      StrCpy $CreatedAbove $R1
      ClearErrors
      CreateDirectory "$INSTDIR"
      ${IfNot} ${FileExists} "$INSTDIR\*.*"
        StrCpy $FolderError "Setup cannot make the folder $INSTDIR."
        StrCpy $FolderCode ${EXIT_FOLDER}
        Return
      ${EndIf}
    ${EndIf}
    StrCpy $R1 $INSTDIR
  ${EndIf}

  ; Writing: make a file, rename it, delete it (Setup does exactly these things when it installs).
  ClearErrors
  FileOpen $R4 "$R1\.okayspace-write-test.tmp" w
  ${If} ${Errors}
    StrCpy $FolderError "Setup cannot write to $R1. The disk may be read-only, or you may not have permission here."
    StrCpy $FolderCode ${EXIT_FOLDER}
    Return
  ${EndIf}
  FileWrite $R4 "test"
  FileClose $R4
  ClearErrors
  Delete "$R1\.okayspace-write-test2.tmp"
  Rename "$R1\.okayspace-write-test.tmp" "$R1\.okayspace-write-test2.tmp"
  ${If} ${Errors}
    Delete "$R1\.okayspace-write-test.tmp"
    StrCpy $FolderError "Setup can make files in $R1 but cannot rename them there. Choose another folder."
    StrCpy $FolderCode ${EXIT_FOLDER}
    Return
  ${EndIf}
  Delete "$R1\.okayspace-write-test2.tmp"
  ${Log} "OK: the folder can be created and written ($R1)"

  ; Free space on that drive, in whole megabytes, against what the files need.
  ClearErrors
  ${DriveSpace} "$R1" "/D=F /S=M" $R5
  ${If} ${Errors}
    ${Log} "WARNING: could not read the free space of the drive with $R1"
  ${Else}
    IntOp $R6 ${DIST_TOTAL_KB} + ${SPACE_MARGIN_KB}
    IntOp $R6 $R6 + 1023
    IntOp $R6 $R6 / 1024
    ${If} $R5 < $R6
      StrCpy $FolderError "There is not enough free space for $INSTDIR: Setup needs about $R6 MB and the drive has $R5 MB free."
      StrCpy $FolderCode ${EXIT_SPACE}
      Return
    ${EndIf}
    ${Log} "OK: free space $R5 MB, needs about $R6 MB"
  ${EndIf}

  ; Warnings (the person decides on the folder page; in silent mode they go in the log).
  StrCpy $R6 "$PROGRAMFILES"
  Call WarnProgramFiles
  StrCpy $R6 "$PROGRAMFILES32"
  Call WarnProgramFiles
  StrCpy $R6 "$PROGRAMFILES64"
  Call WarnProgramFiles

  Push $INSTDIR
  Push "\OneDrive"
  Call Contains
  Pop $R0
  ${If} $R0 == 1
    Call WarnOneDrive
  ${Else}
    ReadEnvStr $R6 "OneDrive"
    Call WarnOneDriveVar
    ReadEnvStr $R6 "OneDriveConsumer"
    Call WarnOneDriveVar
    ReadEnvStr $R6 "OneDriveCommercial"
    Call WarnOneDriveVar
  ${EndIf}

  ; The launcher starts programs with the ANSI code page of Windows, so a folder name with letters outside it breaks starting the
  ; editor and the games (docs/install_audit.md, finding 6). WideCharToMultiByte says when a letter cannot be converted.
  System::Call 'kernel32::WideCharToMultiByte(i 0, i 0x400, w "$INSTDIR", i -1, i 0, i 0, i 0, *i .R7) i .R6'
  ${If} $Quiet == "1"
    SetDetailsPrint none
  ${EndIf}
  ${If} $R7 <> 0
    StrCpy $WarnText "$WarnText$\r$\nThe folder name has letters that are not in your Windows code page. The launcher may not be able to start the editor or games from it. A folder with plain English letters is safer."
    ${Log} "WARNING: the folder name has letters outside the Windows ANSI code page: $INSTDIR"
  ${EndIf}

  StrLen $R6 $INSTDIR
  ${If} $R6 > 150
    StrCpy $WarnText "$WarnText$\r$\nThe folder name is long ($R6 characters). Windows stops at 260 characters for a whole file name, and the editors you install later go deeper."
    ${Log} "WARNING: the folder path is long ($R6 characters): $INSTDIR"
  ${EndIf}
  StrCpy $R0 $WarnText 2
  ${If} $R0 == "$\r$\n"
    StrCpy $WarnText $WarnText "" 2
  ${EndIf}
FunctionEnd

Function WarnProgramFiles
  ${If} $R6 != ""
    StrLen $R7 $R6
    StrCpy $R8 $INSTDIR $R7
    ${If} $R8 == $R6
      ${If} $WarnShown != "pf"
        StrCpy $WarnShown "pf"
        StrCpy $WarnText "$WarnText$\r$\nThe folder is under Program Files. Windows blocks programs from changing files there, so OkaySpace could not update itself or save its settings."
        ${Log} "WARNING: the folder is under Program Files: $INSTDIR"
      ${EndIf}
    ${EndIf}
  ${EndIf}
FunctionEnd

Function WarnOneDrive
  StrCpy $WarnText "$WarnText$\r$\nThe folder looks like it is inside OneDrive. OneDrive may lock files while OkaySpace updates, and it would copy the editor versions (about 75 MB each) to your other PCs."
  ${Log} "WARNING: the folder looks like it is inside OneDrive: $INSTDIR"
FunctionEnd

Function WarnOneDriveVar
  ${If} $R6 != ""
    StrLen $R7 $R6
    StrCpy $R8 $INSTDIR $R7
    ${If} $R8 == $R6
      Call WarnOneDrive
    ${EndIf}
  ${EndIf}
FunctionEnd

; The person pressed Next on the folder page: check, and stay on the page with a message when it cannot be used.
Function DirLeave
  Call TrimInstDir
  StrCpy $R9 "0"
  Call CheckFolder
  ${If} $FolderError != ""
    ${Log} "The folder page: $FolderError"
    MessageBox MB_OK|MB_ICONEXCLAMATION "$FolderError$\r$\n$\r$\nChoose another folder. Nothing was changed." /SD IDOK
    Abort
  ${EndIf}
  ${If} $WarnText != ""
    MessageBox MB_YESNO|MB_ICONEXCLAMATION|MB_DEFBUTTON2 "$WarnText$\r$\n$\r$\nInstall here anyway?" /SD IDYES IDYES ok
    ${Log} "The person chose another folder after the warnings."
    Abort
    ok:
  ${EndIf}
FunctionEnd

; Test every program of the distribution: one that is running cannot be opened for writing.
!macro SCAN_ONE _REL _SIZE
  StrCpy $R7 "${_REL}"
  Call ScanOne
!macroend
Function ScanOne
  ${IfNot} ${FileExists} "$INSTDIR\$R7"
    Return
  ${EndIf}
  SetFileAttributes "$INSTDIR\$R7" NORMAL
  ClearErrors
  FileOpen $R6 "$INSTDIR\$R7" a
  ${If} ${Errors}
    ${If} $RunningList == ""
      StrCpy $RunningList "$R7"
    ${Else}
      StrCpy $RunningList "$RunningList, $R7"
    ${EndIf}
  ${Else}
    FileClose $R6
  ${EndIf}
FunctionEnd
Function ScanRunning
  StrCpy $RunningList ""
  !insertmacro EACH_DIST_PROGRAM SCAN_ONE
FunctionEnd

; Ask to close running programs (Retry / Cancel). Silent mode: no question, the code says why.
Function CheckProgramsClosed
  retry:
  Call ScanRunning
  ${If} $RunningList == ""
    ${Log} "OK: no OkaySpace program is running"
    Return
  ${EndIf}
  ${Log} "OkaySpace is running: $RunningList"
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OkaySpace is still running:$\r$\n$\r$\n$RunningList$\r$\n$\r$\nClose it, then click Retry." /SD IDCANCEL IDRETRY retry
  StrCpy $FailQuiet "1"
  !insertmacro Die ${EXIT_RUNNING} "OkaySpace is running ($RunningList). Close it and run Setup again. Nothing was changed."
FunctionEnd

; An older Setup over a newer install: ask (normal mode) or refuse (silent mode) unless /FORCE.
Function CheckVersion
  ${If} $OldVersion == ""
    Return
  ${EndIf}
  Push "${VERSION}"
  Call ValidVersion
  Pop $R0
  ${If} $R0 != 1
    ${Log} "This Setup's version (${VERSION}) is not a plain number, so it is not compared with the installed version ($OldVersion)."
    Return
  ${EndIf}
  ${VersionCompare} "${VERSION}" "$OldVersion" $R0
  ${If} $R0 == 2
    ${If} $ForceFlag == "1"
      ${Log} "WARNING: replacing the newer installed version $OldVersion with ${VERSION} because of /FORCE"
      Return
    ${EndIf}
    ${Log} "The installed version ($OldVersion) is newer than this Setup (${VERSION}). Run Setup with /FORCE to replace it anyway."
    MessageBox MB_YESNO|MB_ICONEXCLAMATION|MB_DEFBUTTON2 "A newer OkaySpace ($OldVersion) is already installed here. This Setup is version ${VERSION}.$\r$\n$\r$\nInstalling it would replace the newer programs with older ones.$\r$\n$\r$\nInstall the older version anyway?" /SD IDNO IDYES older
    StrCpy $FailQuiet "1"
    !insertmacro Die ${EXIT_NEWER} "A newer OkaySpace ($OldVersion) is already installed here and this Setup is ${VERSION}. Nothing was changed. Run Setup with /FORCE to replace it anyway."
    older:
    ${Log} "The person chose to replace the newer version $OldVersion with ${VERSION}."
  ${Else}
    ${Log} "OK: this Setup (${VERSION}) is not older than the installed version ($OldVersion)"
  ${EndIf}
FunctionEnd

; ---- the staged copy -----------------------------------------------------------------------------------------------------------
; EACH_DIST_FILE gives (path, size). Each of these helpers works on the path in $R7 (and the size in $R8).
!macro VERIFY_STAGE_ONE _REL _SIZE
  StrCpy $R7 "${_REL}"
  StrCpy $R8 ${_SIZE}
  StrCpy $R9 "$StageDir"
  Call VerifyOne
!macroend
!macro VERIFY_FINAL_ONE _REL _SIZE
  StrCpy $R7 "${_REL}"
  StrCpy $R8 ${_SIZE}
  StrCpy $R9 "$INSTDIR"
  Call VerifyOne
!macroend
; $R9 = folder, $R7 = path, $R8 = size it must have.
Function VerifyOne
  ${If} $VerifyError != ""
    Return
  ${EndIf}
  Push "$R9\$R7"
  Call FileSize
  Pop $R6
  ${If} $R6 <> $R8
    StrCpy $VerifyError "$R7 (expected $R8 bytes, found $R6)"
  ${Else}
    IntOp $VerifyCount $VerifyCount + 1
  ${EndIf}
FunctionEnd
Function VerifyStage
  StrCpy $VerifyError ""
  StrCpy $VerifyCount 0
  !insertmacro EACH_DIST_FILE VERIFY_STAGE_ONE
FunctionEnd
Function VerifyFinal
  StrCpy $VerifyError ""
  StrCpy $VerifyCount 0
  !insertmacro EACH_DIST_FILE VERIFY_FINAL_ONE
  ; the two files Setup writes itself: they must exist and not be empty
  Push "$INSTDIR\Uninstall.exe"
  Call FileSize
  Pop $R6
  ${If} $R6 <= 0
    StrCpy $VerifyError "Uninstall.exe"
  ${EndIf}
  Push "$INSTDIR\${MANIFEST_NAME}"
  Call FileSize
  Pop $R6
  ${If} $R6 <= 0
    StrCpy $VerifyError "${MANIFEST_NAME}"
  ${EndIf}
FunctionEnd

; Move $INSTDIR\<path> aside to <path>.old and move $StageDir\<path> in. On an error nothing of this file is left half done.
!macro SWAP_ONE _REL _SIZE
  StrCpy $R7 "${_REL}"
  Call SwapOne
!macroend
Function SwapOne
  ${If} $SwapError != ""
    Return
  ${EndIf}
  ${GetParent} "$INSTDIR\$R7" $R6
  ${IfNot} ${FileExists} "$R6\*.*"
    ClearErrors
    CreateDirectory "$R6"
    ${IfNot} ${FileExists} "$R6\*.*"
      StrCpy $SwapError "could not make the folder $R6"
      Return
    ${EndIf}
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\$R7\*.*"
    StrCpy $SwapError "a folder is where the file $R7 should go"
    Return
  ${EndIf}
  ; a backup left by an earlier run
  ${If} ${FileExists} "$INSTDIR\$R7.old"
    SetFileAttributes "$INSTDIR\$R7.old" NORMAL
    Delete "$INSTDIR\$R7.old"
  ${EndIf}
  ; A virus scanner or an open Explorer window can hold a file for a moment, so each rename is tried a few times.
  StrCpy $R5 0
  ${If} ${FileExists} "$INSTDIR\$R7"
    StrCpy $R4 0
    aside:
      ClearErrors
      Rename "$INSTDIR\$R7" "$INSTDIR\$R7.old"
      ${If} ${Errors}
        IntOp $R4 $R4 + 1
        ${If} $R4 < 5
          Sleep 300
          Goto aside
        ${EndIf}
        StrCpy $SwapError "could not move the old $R7 aside"
        Return
      ${EndIf}
    StrCpy $R5 1
  ${EndIf}
  StrCpy $R4 0
  movein:
    ClearErrors
    Rename "$StageDir\$R7" "$INSTDIR\$R7"
    ${If} ${Errors}
      IntOp $R4 $R4 + 1
      ${If} $R4 < 5
        Sleep 300
        Goto movein
      ${EndIf}
      StrCpy $SwapError "could not move the new $R7 into place"
      ${If} $R5 == 1
        Rename "$INSTDIR\$R7.old" "$INSTDIR\$R7"
      ${EndIf}
      Return
    ${EndIf}
  IntOp $SwapCount $SwapCount + 1
FunctionEnd

; Put one file back: the old one comes back from <path>.old; a file that had no old one (new in this release) is removed. Only the
; first $RbLimit files in swap order are touched (-1: only those that have a .old, which is what is known after a crash).
!macro ROLLBACK_ONE _REL _SIZE
  StrCpy $R7 "${_REL}"
  Call RollbackOne
!macroend
Function RollbackOne
  IntOp $RbIndex $RbIndex + 1
  ${If} $RbLimit <> -1
  ${AndIf} $RbIndex > $RbLimit
    Return
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\$R7.old"
    ${IfNot} ${FileExists} "$INSTDIR\$R7.old\*.*"
      SetFileAttributes "$INSTDIR\$R7" NORMAL
      Delete "$INSTDIR\$R7"
      ClearErrors
      Rename "$INSTDIR\$R7.old" "$INSTDIR\$R7"
      ${IfNot} ${Errors}
        IntOp $RbCount $RbCount + 1
      ${EndIf}
    ${EndIf}
  ${ElseIf} $RbLimit <> -1
    ${If} ${FileExists} "$INSTDIR\$R7"
      SetFileAttributes "$INSTDIR\$R7" NORMAL
      Delete "$INSTDIR\$R7"
    ${EndIf}
  ${EndIf}
FunctionEnd
; The same for the two files Setup writes itself (they are not part of the swap order, so no index): the old one comes back from its
; .old; one with no old copy is removed only when its staged copy is gone, which proves it was moved in.
Function RollbackExtraOne
  ${If} ${FileExists} "$INSTDIR\$R7.old"
    ${IfNot} ${FileExists} "$INSTDIR\$R7.old\*.*"
      SetFileAttributes "$INSTDIR\$R7" NORMAL
      Delete "$INSTDIR\$R7"
      ClearErrors
      Rename "$INSTDIR\$R7.old" "$INSTDIR\$R7"
      ${IfNot} ${Errors}
        IntOp $RbCount $RbCount + 1
      ${EndIf}
    ${EndIf}
  ${ElseIf} $RbLimit <> -1
    ${IfNot} ${FileExists} "$StageDir\$R7"
      ${If} ${FileExists} "$INSTDIR\$R7"
        SetFileAttributes "$INSTDIR\$R7" NORMAL
        Delete "$INSTDIR\$R7"
      ${EndIf}
    ${EndIf}
  ${EndIf}
FunctionEnd
Function RollbackExtras
  StrCpy $R7 "Uninstall.exe"
  Call RollbackExtraOne
  StrCpy $R7 "${MANIFEST_NAME}"
  Call RollbackExtraOne
FunctionEnd

; Sets $R5 to 1 when the path in $R4 is one of the files of this release.
!macro MEMBER_ONE _REL _SIZE
  StrCmp $R4 "${_REL}" 0 +2
  StrCpy $R5 1
!macroend

; The files an earlier Setup installed that this release no longer ships ($INSTDIR\installed-files.txt lists them): they are moved aside
; as <path>.old too, and listed in $StageDir\stale.txt so that a rollback or the commit knows them. A file that cannot be moved is left.
Function StaleSwap
  ${IfNot} ${FileExists} "$INSTDIR\${MANIFEST_NAME}"
    Return
  ${EndIf}
  ClearErrors
  FileOpen $R0 "$INSTDIR\${MANIFEST_NAME}" r
  ${If} ${Errors}
    Return
  ${EndIf}
  FileOpen $R1 "$StageDir\stale.txt" w
  ${Do}
    ClearErrors
    FileRead $R0 $R2
    ${If} ${Errors}
      ${Break}
    ${EndIf}
    Push $R2
    Call CleanLine
    Pop $R2
    ${If} $R2 != ""
      ; is it in the new list?
      StrCpy $R4 $R2
      StrCpy $R5 0
      !insertmacro EACH_DIST_FILE MEMBER_ONE
      ${If} $R5 == 0
        Push $R2
        Call SafeStalePath
        Pop $R3
        ${If} $R3 == 1
        ${AndIf} ${FileExists} "$INSTDIR\$R2"
          ${IfNot} ${FileExists} "$INSTDIR\$R2\*.*"
            ${If} ${FileExists} "$INSTDIR\$R2.old"
              SetFileAttributes "$INSTDIR\$R2.old" NORMAL
              Delete "$INSTDIR\$R2.old"
            ${EndIf}
            ClearErrors
            Rename "$INSTDIR\$R2" "$INSTDIR\$R2.old"
            ${If} ${Errors}
              ${Log} "WARNING: could not remove $R2, which this release no longer ships"
            ${Else}
              FileWrite $R1 "$R2$\r$\n"
            ${EndIf}
          ${EndIf}
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${Loop}
  FileClose $R1
  FileClose $R0
FunctionEnd

; Put the stale files back (rollback) or throw their backups away (commit). Mode in $R9: "back" or "drop".
Function StaleEnd
  ${IfNot} ${FileExists} "$StageDir\stale.txt"
    Return
  ${EndIf}
  ClearErrors
  FileOpen $R0 "$StageDir\stale.txt" r
  ${If} ${Errors}
    Return
  ${EndIf}
  ${Do}
    ClearErrors
    FileRead $R0 $R2
    ${If} ${Errors}
      ${Break}
    ${EndIf}
    Push $R2
    Call CleanLine
    Pop $R2
    ${If} $R2 != ""
      ${If} $R9 == "back"
        ${If} ${FileExists} "$INSTDIR\$R2.old"
          Rename "$INSTDIR\$R2.old" "$INSTDIR\$R2"
        ${EndIf}
      ${Else}
        SetFileAttributes "$INSTDIR\$R2.old" NORMAL
        Delete "$INSTDIR\$R2.old"
        ; remove folders that are now empty (a folder that still holds something stays)
        ${GetParent} "$INSTDIR\$R2" $R3
        StrCpy $R4 0
        ${Do}
          ${If} $R3 == $INSTDIR
          ${OrIf} $R3 == ""
          ${OrIf} $R4 > 10
            ${Break}
          ${EndIf}
          RMDir "$R3"
          ${GetParent} "$R3" $R3
          IntOp $R4 $R4 + 1
        ${Loop}
      ${EndIf}
    ${EndIf}
  ${Loop}
  FileClose $R0
FunctionEnd

; Write the list of files this Setup installed (the next Setup uses it to remove files a release drops; the uninstaller reads it too).
!macro MANIFEST_ONE _REL _SIZE
  FileWrite $R0 "${_REL}$\r$\n"
!macroend
Function WriteManifest
  ClearErrors
  FileOpen $R0 "$StageDir\${MANIFEST_NAME}" w
  ${If} ${Errors}
    Return
  ${EndIf}
  FileWrite $R0 "# Files OkaySpace Setup ${VERSION} installed. Setup and the uninstaller read this list. Please do not edit it.$\r$\n"
  !insertmacro EACH_DIST_FILE MANIFEST_ONE
  FileClose $R0
FunctionEnd

!macro RMDIR_DIST_ONE _REL
  RMDir "$INSTDIR\${_REL}"
!macroend

; What a failed or crashed run leaves: put the old files back and remove the staging folder. Safe to run twice.
Function CleanupAfterFailure
  ${If} $CleanedUp == "1"
    Return
  ${EndIf}
  StrCpy $CleanedUp "1"
  ${If} $StageDir == ""
    Return
  ${EndIf}
  ${If} $SwapStarted == "1"
    StrCpy $RbIndex 0
    StrCpy $RbCount 0
    StrCpy $RbLimit $SwapCount
    Call RollbackExtras
    StrCpy $R9 "back"
    Call StaleEnd
    !insertmacro EACH_DIST_FILE ROLLBACK_ONE
    ${Log} "Rolled back: restored $RbCount old files, removed the new files that had no old ones"
  ${EndIf}
  ${If} ${FileExists} "$StageDir\*.*"
    SetOutPath "$INSTDIR"
    RMDir /r "$StageDir"
  ${EndIf}
  ; Folders left empty by the rollback, and the install folder itself when Setup made it.
  SetOutPath "$TEMP"
  !insertmacro EACH_DIST_DIR RMDIR_DIST_ONE
  ${If} $CreatedAbove != ""
    StrCpy $R0 $INSTDIR
    StrCpy $R1 0
    ${Do}
      ${If} $R0 == $CreatedAbove
      ${OrIf} $R0 == ""
      ${OrIf} $R1 > 30
        ${Break}
      ${EndIf}
      RMDir "$R0"
      ${GetParent} "$R0" $R0
      IntOp $R1 $R1 + 1
    ${Loop}
  ${EndIf}
FunctionEnd

; A crashed or cancelled earlier run left a staging folder: if it was in the middle of swapping, put the old files back first.
Function RecoverLeftovers
  ${IfNot} ${FileExists} "$StageDir\*.*"
    Return
  ${EndIf}
  ${Log} "Found the staging folder of an unfinished Setup in $StageDir"
  ${If} ${FileExists} "$StageDir\.swap-started"
    StrCpy $RbIndex 0
    StrCpy $RbCount 0
    StrCpy $RbLimit -1
    Call RollbackExtras
    StrCpy $R9 "back"
    Call StaleEnd
    !insertmacro EACH_DIST_FILE ROLLBACK_ONE
    ${Log} "The unfinished Setup was in the middle of swapping files; restored $RbCount old files"
  ${EndIf}
  RMDir /r "$StageDir"
FunctionEnd

; Push the exit code, then the message (use the Die macro).
Function Fail
  Pop $R1
  Pop $R0
  StrCpy $FailCode $R0
  ${Log} "FAILED: $R1"
  Call CleanupAfterFailure
  ${Log} "Exit code $FailCode."
  StrCpy $Quiet ""
  SetDetailsPrint both
  ${If} $FailQuiet != "1"
    MessageBox MB_OK|MB_ICONSTOP "$R1$\r$\n$\r$\nThe details are in $LogFile" /SD IDOK
  ${EndIf}
  SetErrorLevel $FailCode
  Abort
FunctionEnd

; ---- pages -------------------------------------------------------------------------------------------------------------------
Function SkipDirIfUpdate
  ${If} $IsUpdate == "1"
    Abort                    ; already installed -> don't ask where, just update it
  ${EndIf}
FunctionEnd

!define MUI_ABORTWARNING
!define MUI_CUSTOMFUNCTION_ABORT OnSetupAbort
!define MUI_FINISHPAGE_RUN "$INSTDIR\OkaySpace.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch OkaySpace now"
!define MUI_WELCOMEPAGE_TITLE "Welcome to the OkaySpace setup"
!define MUI_WELCOMEPAGE_TEXT "This installs or updates OkaySpace ${VERSION} for your Windows account only. It does not need administrator rights.$\r$\n$\r$\nYour projects are not in Documents. By default they are in the Projects folder inside the OkaySpace folder (%LOCALAPPDATA%\OkaySpace\Projects), together with your settings, your sign-in, the official game and the editors you installed on the Versions page.$\r$\n$\r$\nAn update keeps all of that. It replaces only the program files Setup installs, including the example scenes, so keep your own work outside the examples folder.$\r$\n$\r$\nBefore it changes anything, Setup checks the folder, the free space and that OkaySpace is closed. A log is written to %TEMP%\${SETUP_LOG}."

!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipDirIfUpdate
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

UninstPage custom un.OptionsCreate un.OptionsLeave
!insertmacro MUI_UNPAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_SHOW un.FinishShow
!define MUI_FINISHPAGE_TITLE "OkaySpace was uninstalled"
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; ---- the install ---------------------------------------------------------------------------------------------------------------
; Throw away the backup of one file after a successful swap.
!macro DROP_OLD_ONE _REL _SIZE
  ${If} ${FileExists} "$INSTDIR\${_REL}.old"
    SetFileAttributes "$INSTDIR\${_REL}.old" NORMAL
    Delete "$INSTDIR\${_REL}.old"
  ${EndIf}
!macroend

Section "OkaySpace" SecMain
  SetDetailsPrint both
  ${If} $IsUpdate == "1"
    DetailPrint "Updating OkaySpace in $INSTDIR"
  ${Else}
    DetailPrint "Installing OkaySpace in $INSTDIR"
  ${EndIf}
  DetailPrint "Checking the folder, the free space and that OkaySpace is closed..."
  StrCpy $Quiet "1"
  SetDetailsPrint none            ; the checks and the file swapping below do many small steps; the log has them
  ${Log} "Installing OkaySpace ${VERSION} into $INSTDIR"
  StrCpy $StageDir "$INSTDIR\${STAGE_NAME}"
  StrCpy $CleanedUp ""
  StrCpy $SwapStarted ""
  StrCpy $SwapCount 0

  ; 1. Everything that can go wrong is checked before anything is changed.
  StrCpy $R9 "1"
  Call CheckFolder
  ${If} $FolderError != ""
    !insertmacro Die $FolderCode "$FolderError Nothing was installed."
  ${EndIf}
  Call RecoverLeftovers
  Call CheckVersion
  Call CheckProgramsClosed

  ; 2. Unpack beside the old files and check every file.
  RMDir /r "$StageDir"
  CreateDirectory "$StageDir"
  ${IfNot} ${FileExists} "$StageDir\*.*"
    !insertmacro Die ${EXIT_UNPACK} "Setup cannot make the folder $StageDir. Nothing was changed."
  ${EndIf}
  SetOutPath "$StageDir"
  SetOverwrite on
  StrCpy $Quiet ""
  SetDetailsPrint both
  DetailPrint "Unpacking the new files beside the old ones..."
  ${Log} "Unpacking ${DIST_FILE_COUNT} files (${DIST_TOTAL_BYTES} bytes) into $StageDir"
  ; Forward slash + "*" so this resolves on the build host whether makensis runs on Windows or (as in CI) on Linux, where "\" is
  ; literal and "*.*" would skip extensionless files.
  File /r "${DISTDIR}/*"
  StrCpy $Quiet "1"
  SetDetailsPrint none
  ; The unpacking left the staging folder as the current folder, and Windows will not remove a current folder. Move out of it.
  SetOutPath "$INSTDIR"
  Call VerifyStage
  ${If} $VerifyError != ""
    !insertmacro Die ${EXIT_UNPACK} "A file was not unpacked completely: $VerifyError. The disk may be full. Nothing was changed."
  ${EndIf}
  ${Log} "OK: unpacked $VerifyCount files and checked every size"

  ; The uninstaller and the file list are made in the staging folder too, so they are swapped in with everything else.
  WriteUninstaller "$StageDir\Uninstall.exe"
  Call WriteManifest
  Push "$StageDir\Uninstall.exe"
  Call FileSize
  Pop $R0
  Push "$StageDir\${MANIFEST_NAME}"
  Call FileSize
  Pop $R1
  ${If} $R0 <= 0
  ${OrIf} $R1 <= 0
    !insertmacro Die ${EXIT_UNPACK} "Setup could not write the uninstaller or the file list. The disk may be full. Nothing was changed."
  ${EndIf}

  ; 3. Swap. From the first rename on, a crash is recoverable (the marker), and any error puts everything back.
  StrCpy $Quiet ""
  SetDetailsPrint both
  DetailPrint "Replacing the old files (they are kept until everything is in place)..."
  StrCpy $Quiet "1"
  SetDetailsPrint none
  FileOpen $R0 "$StageDir\.swap-started" w
  FileWrite $R0 "swapping"
  FileClose $R0
  StrCpy $SwapStarted "1"
  StrCpy $SwapError ""
  !insertmacro EACH_DIST_FILE SWAP_ONE
  ${If} $SwapError == ""
    Call StaleSwap
    StrCpy $R7 "Uninstall.exe"
    Call SwapOne
    StrCpy $R7 "${MANIFEST_NAME}"
    Call SwapOne
  ${EndIf}
  ${If} $SwapError != ""
    !insertmacro Die ${EXIT_SWAP} "Setup could not put the new files in place ($SwapError). Your old version was restored."
  ${EndIf}
  Call VerifyFinal
  ${If} $VerifyError != ""
    !insertmacro Die ${EXIT_SWAP} "The new files are not complete after the copy ($VerifyError). Your old version was restored."
  ${EndIf}
  ${Log} "OK: moved $SwapCount files into place and checked them"

  ; 4. Commit: from here the new version stays. Removing the marker first means a crash after this point is only leftovers.
  Delete "$StageDir\.swap-started"
  StrCpy $R9 "drop"
  Call StaleEnd
  !insertmacro EACH_DIST_FILE DROP_OLD_ONE
  Delete "$INSTDIR\Uninstall.exe.old"
  Delete "$INSTDIR\${MANIFEST_NAME}.old"
  RMDir /r "$StageDir"

  ; 5. Only now the shortcuts and the Apps & features entry (they point at files that are in place).
  StrCpy $Quiet ""
  SetDetailsPrint both
  DetailPrint "Making the shortcuts and the Apps & features entry..."
  StrCpy $Quiet "1"
  SetDetailsPrint none
  Call Register
  StrCpy $Quiet ""
  SetDetailsPrint both
  DetailPrint "Done."
  ${Log} "OK: shortcuts and the Apps & features entry are written"
  ; Logged here, not in .onInstSuccess: that runs only when the person closes the Finish page, and a log that stops before its last line
  ; looks like a crash.
  ${Log} "Setup finished. Exit code 0."
SectionEnd

Function Register
  SetOutPath "$INSTDIR"            ; the shortcuts start OkaySpace in its own folder (it keeps its settings there)
  CreateDirectory "$SMPROGRAMS\OkaySpace"
  CreateShortcut "$SMPROGRAMS\OkaySpace\OkaySpace.lnk" "$INSTDIR\OkaySpace.exe" "" "$INSTDIR\OkaySpace.exe" 0
  CreateShortcut "$SMPROGRAMS\OkaySpace\Uninstall OkaySpace.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
  Delete "$SMPROGRAMS\OkaySpace.lnk"   ; the loose shortcut an older Setup made

  ; The Desktop shortcut is made on the first install only, and the choice is remembered, so a shortcut the person deleted does not
  ; come back with the next update.
  ClearErrors
  ReadRegDWORD $R0 HKCU "${APP_KEY}" "DesktopShortcut"
  ${If} ${Errors}
    ${If} $IsUpdate == "1"
      ; an install an older Setup made: do not guess, just remember what is there
      ${If} ${FileExists} "$DESKTOP\OkaySpace.lnk"
        WriteRegDWORD HKCU "${APP_KEY}" "DesktopShortcut" 1
      ${Else}
        WriteRegDWORD HKCU "${APP_KEY}" "DesktopShortcut" 0
      ${EndIf}
    ${Else}
      CreateShortcut "$DESKTOP\OkaySpace.lnk" "$INSTDIR\OkaySpace.exe" "" "$INSTDIR\OkaySpace.exe" 0
      WriteRegDWORD HKCU "${APP_KEY}" "DesktopShortcut" 1
    ${EndIf}
  ${EndIf}

  ClearErrors
  WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"
  ${If} ${Errors}
    ${Log} "WARNING: could not write HKCU\${APP_KEY}"
  ${EndIf}
  ${GetTime} "" "L" $R1 $R2 $R3 $R4 $R5 $R6 $R7
  WriteRegStr   HKCU "${ARP_KEY}" "DisplayName"          "OkaySpace"
  WriteRegStr   HKCU "${ARP_KEY}" "DisplayVersion"       "${VERSION}"
  WriteRegStr   HKCU "${ARP_KEY}" "Publisher"            "OkaySpace"
  WriteRegStr   HKCU "${ARP_KEY}" "DisplayIcon"          "$INSTDIR\OkaySpace.exe"
  WriteRegStr   HKCU "${ARP_KEY}" "InstallLocation"      "$INSTDIR"
  WriteRegStr   HKCU "${ARP_KEY}" "InstallDate"          "$R3$R2$R1"
  WriteRegStr   HKCU "${ARP_KEY}" "UninstallString"      '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKCU "${ARP_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${ARP_KEY}" "EstimatedSize"        ${DIST_TOTAL_KB}
  WriteRegDWORD HKCU "${ARP_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${ARP_KEY}" "NoRepair" 1
  ; No support link on purpose (owner decision: none until there is a stable page).
FunctionEnd

; NSIS calls this after a failure it found itself (for example the disk filled while unpacking): put everything back.
Function .onInstFailed
  Call CleanupAfterFailure
  ${If} $FailCode == ""
    StrCpy $FailCode ${EXIT_UNPACK}
    ${Log} "FAILED: Setup stopped while unpacking or copying files (the disk may be full)."
    ${Log} "Exit code $FailCode."
    SetErrorLevel $FailCode
  ${EndIf}
FunctionEnd

; ===============================================================================================================================
; The uninstaller
; ===============================================================================================================================

; Test one program of the distribution (the path is in $R7) in the uninstaller: running programs cannot be opened for writing.
!macro UN_SCAN_ONE _REL _SIZE
  StrCpy $R7 "${_REL}"
  Call un.ScanOne
!macroend
Function un.ScanOne
  ${IfNot} ${FileExists} "$INSTDIR\$R7"
    Return
  ${EndIf}
  SetFileAttributes "$INSTDIR\$R7" NORMAL
  ClearErrors
  FileOpen $R6 "$INSTDIR\$R7" a
  ${If} ${Errors}
    ${If} $RunningList == ""
      StrCpy $RunningList "$R7"
    ${Else}
      StrCpy $RunningList "$RunningList, $R7"
    ${EndIf}
  ${Else}
    FileClose $R6
  ${EndIf}
FunctionEnd

; Every .exe below the folder in $R0 that is running (kept editors in Versions, the official game in Games).
Function un.ScanTree
  Push $R1
  Push $R2
  Push $R3
  Push $R4
  FindFirst $R1 $R2 "$R0\*.*"
  ${DoUntil} $R2 == ""
    ${If} $R2 != "."
    ${AndIf} $R2 != ".."
      StrCpy $R3 "$R0\$R2"
      ${If} ${FileExists} "$R3\*.*"
        System::Call 'kernel32::GetFileAttributesW(w "$R3") i .R4'
        IntOp $R4 $R4 & 0x400
        ${If} $R4 = 0
          Push $R0
          StrCpy $R0 $R3
          Call un.ScanTree
          Pop $R0
        ${EndIf}
      ${Else}
        StrCpy $R4 $R2 4 -4
        ${If} $R4 == ".exe"
          ClearErrors
          SetFileAttributes "$R3" NORMAL
          FileOpen $R4 "$R3" a
          ${If} ${Errors}
            StrLen $R4 "$INSTDIR\"
            StrCpy $R4 $R3 "" $R4
            ${If} $RunningList == ""
              StrCpy $RunningList "$R4"
            ${Else}
              StrCpy $RunningList "$RunningList, $R4"
            ${EndIf}
          ${Else}
            FileClose $R4
          ${EndIf}
        ${EndIf}
      ${EndIf}
    ${EndIf}
    FindNext $R1 $R2
  ${Loop}
  FindClose $R1
  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
FunctionEnd

Function un.ScanRunning
  StrCpy $RunningList ""
  !insertmacro EACH_DIST_PROGRAM UN_SCAN_ONE
  StrCpy $R0 "$INSTDIR\Versions"
  Call un.ScanTree
  StrCpy $R0 "$INSTDIR\Games"
  Call un.ScanTree
FunctionEnd

; Push the exit code, then the message.
!macro UnDie _CODE _TEXT
  Push ${_CODE}
  Push "${_TEXT}"
  Call un.Fail
!macroend
Function un.Fail
  Pop $R1
  Pop $R0
  StrCpy $FailCode $R0
  ${LogU} "FAILED: $R1"
  ${LogU} "Exit code $FailCode."
  ${If} $FailQuiet != "1"
    MessageBox MB_OK|MB_ICONSTOP "$R1$\r$\n$\r$\nThe details are in $LogFile" /SD IDOK
  ${EndIf}
  SetErrorLevel $FailCode
  Abort
FunctionEnd

; Push a folder, Call, Pop "1" when it holds a project: it has a project.okayproj, or an Assets folder (the editor makes Assets in every
; project, and the Create page lists a folder with an Assets folder as a project).
Function un.HoldsProject
  Exch $R0
  ${If} ${FileExists} "$R0\project.okayproj"
  ${OrIf} ${FileExists} "$R0\Assets\*.*"
    StrCpy $R0 1
  ${Else}
    StrCpy $R0 0
  ${EndIf}
  Exch $R0
FunctionEnd

; Remember what was kept: the last page shows the first lines, the log has all of them. Push the path, then the reason.
Function un.Kept
  Pop $R1
  Pop $R0
  ${LogU} "Kept: $R0 ($R1)"
  ; the last page has little room: show the path from the install folder (or %APPDATA%), not the whole path
  ${If} $R0 == $INSTDIR
    StrCpy $R3 "the OkaySpace folder"
  ${Else}
    StrLen $R4 "$INSTDIR\"
    StrCpy $R5 $R0 $R4
    ${If} $R5 == "$INSTDIR\"
      StrCpy $R3 $R0 "" $R4
    ${Else}
      StrLen $R4 "$APPDATA\"
      StrCpy $R5 $R0 $R4
      ${If} $R5 == "$APPDATA\"
        StrCpy $R3 $R0 "" $R4
        StrCpy $R3 "%APPDATA%\$R3"
      ${Else}
        StrLen $R4 "$LOCALAPPDATA\"
        StrCpy $R5 $R0 $R4
        ${If} $R5 == "$LOCALAPPDATA\"
          StrCpy $R3 $R0 "" $R4
          StrCpy $R3 "%LOCALAPPDATA%\$R3"
        ${Else}
          StrCpy $R3 $R0
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${EndIf}
  StrLen $R2 $UnKept
  ${If} $R2 < 700
    StrCpy $UnKept "$UnKept$\r$\nKept: $R3 ($R1)"
  ${Else}
    StrCpy $UnKeptMore "1"
  ${EndIf}
FunctionEnd

; Removes everything inside the folder in $R0 and then the folder. With $UnSweepProjects = "1" a folder that holds a project is
; left whole and reported. A link (junction, symbolic link) is removed itself, never followed, so what it points at is not touched.
; Out: $R1 = "1" when something stays, so the folder could not be removed.
Function un.Sweep
  Push $R2
  Push $R3
  Push $R4
  Push $R5
  Push $R6
  StrCpy $R6 0
  System::Call 'kernel32::GetFileAttributesW(w "$R0") i .R5'
  IntOp $R5 $R5 & 0x400
  ${If} $R5 <> 0
    RMDir "$R0"
    ${If} ${FileExists} "$R0"
      SetFileAttributes "$R0" NORMAL
      Delete "$R0"                  ; a symbolic link to a folder is taken away with Delete under wine; RMDir does it on Windows
    ${EndIf}
    ${If} ${FileExists} "$R0"
      StrCpy $R1 1
    ${Else}
      StrCpy $R1 0
      ${LogU} "Removed the link $R0 (what it points at was not touched)"
    ${EndIf}
    Goto swept
  ${EndIf}
  ${If} $UnSweepProjects == "1"
    Push $R0
    Call un.HoldsProject
    Pop $R5
    ${If} $R5 == 1
      Push $R0
      Push "a project"
      Call un.Kept
      StrCpy $R1 1
      Goto swept
    ${EndIf}
  ${EndIf}
  FindFirst $R2 $R3 "$R0\*.*"
  ${DoUntil} $R3 == ""
    ${If} $R3 != "."
    ${AndIf} $R3 != ".."
      StrCpy $R4 "$R0\$R3"
      ${If} ${FileExists} "$R4\*.*"
        Push $R0
        StrCpy $R0 $R4
        Call un.Sweep
        Pop $R0
        ${If} $R1 == 1
          StrCpy $R6 1
        ${EndIf}
      ${Else}
        SetFileAttributes "$R4" NORMAL
        Delete "$R4"
        ${If} ${FileExists} "$R4"
          StrCpy $R6 1
          ${LogU} "Could not remove (in use?): $R4"
        ${Else}
          IntOp $UnGone $UnGone + 1
        ${EndIf}
      ${EndIf}
    ${EndIf}
    FindNext $R2 $R3
  ${Loop}
  FindClose $R2
  ${If} $R6 = 0
    RMDir "$R0"
    ${If} ${FileExists} "$R0"
      StrCpy $R6 1
    ${EndIf}
  ${EndIf}
  StrCpy $R1 $R6
  swept:
  Pop $R6
  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R2
FunctionEnd

; Delete one file of the distribution, and its backup from a swap.
!macro UN_DELETE_ONE _REL _SIZE
  SetFileAttributes "$INSTDIR\${_REL}" NORMAL
  Delete "$INSTDIR\${_REL}"
  Delete "$INSTDIR\${_REL}.old"
!macroend
!macro UN_RMDIR_ONE _REL
  RMDir "$INSTDIR\${_REL}"
!macroend

; The files named in installed-files.txt (what the last Setup installed). It can name files this build does not know, for example
; from an older release; the same safety rules as for an update apply.
Function un.DeleteListed
  ${IfNot} ${FileExists} "$INSTDIR\${MANIFEST_NAME}"
    Return
  ${EndIf}
  ClearErrors
  FileOpen $R0 "$INSTDIR\${MANIFEST_NAME}" r
  ${If} ${Errors}
    Return
  ${EndIf}
  ${Do}
    ClearErrors
    FileRead $R0 $R2
    ${If} ${Errors}
      ${Break}
    ${EndIf}
    Push $R2
    Call un.CleanLine
    Pop $R2
    ${If} $R2 != ""
      Push $R2
      Call un.SafeStalePath
      Pop $R3
      ${If} $R3 == 1
      ${AndIfNot} ${FileExists} "$INSTDIR\$R2\*.*"
        SetFileAttributes "$INSTDIR\$R2" NORMAL
        Delete "$INSTDIR\$R2"
        ${GetParent} "$INSTDIR\$R2" $R3
        StrCpy $R4 0
        ${Do}
          ${If} $R3 == $INSTDIR
          ${OrIf} $R3 == ""
          ${OrIf} $R4 > 10
            ${Break}
          ${EndIf}
          RMDir "$R3"
          ${GetParent} "$R3" $R3
          IntOp $R4 $R4 + 1
        ${Loop}
      ${EndIf}
    ${EndIf}
  ${Loop}
  FileClose $R0
FunctionEnd
; Remove every file in the temp folder that OkaySpace programs left (their names start with okayspace_).
Function un.CleanTemp
  FindFirst $R1 $R2 "$TEMP\okayspace_*"
  ${DoUntil} $R2 == ""
    ${If} ${FileExists} "$TEMP\$R2\*.*"
      RMDir /r "$TEMP\$R2"
    ${Else}
      Delete "$TEMP\$R2"
    ${EndIf}
    FindNext $R1 $R2
  ${Loop}
  FindClose $R1
FunctionEnd

; ---- the uninstall pages ---------------------------------------------------------------------------------------------------------
Function un.onInit
  StrCpy $LogFile "$TEMP\${UNINST_LOG}"
  Call un.LogStart
  ${LogU} "Uninstall started: OkaySpace uninstaller for ${VERSION}, folder $INSTDIR"
  ${un.GetParameters} $R0
  ${LogU} "Command line: $R0"
  ${un.GetOptions} $R0 "/REMOVEUSERDATA" $R1
  ${IfNot} ${Errors}
    StrCpy $UnRemoveData "1"
  ${EndIf}
  ; Only in an OkaySpace folder: Uninstall.exe copied somewhere else (Downloads, the Desktop) would otherwise clean that folder.
  ReadRegStr $R2 HKCU "${APP_KEY}" "InstallDir"
  ${If} ${FileExists} "$INSTDIR\OkaySpace.exe"
  ${OrIf} ${FileExists} "$INSTDIR\${MANIFEST_NAME}"
  ${OrIf} $R2 == $INSTDIR
    ${LogU} "OK: $INSTDIR is an OkaySpace folder"
  ${Else}
    ${LogU} "FAILED: Uninstall.exe is in $INSTDIR, which is not an OkaySpace folder. Nothing was removed."
    MessageBox MB_OK|MB_ICONSTOP "This Uninstall.exe is in $INSTDIR, which is not an OkaySpace folder, so it will not remove anything.$\r$\n$\r$\nUse Settings > Apps > Installed apps > OkaySpace instead." /SD IDOK
    ${LogU} "Exit code ${EXIT_NOTOURS}."
    SetErrorLevel ${EXIT_NOTOURS}
    Abort
  ${EndIf}
  System::Call 'kernel32::CreateMutexW(i 0, i 0, w "${MUTEX_NAME}") i .r1 ?e'
  Pop $R0
  ${If} $R0 = 183
    ${LogU} "FAILED: another OkaySpace Setup or uninstall is already running"
    MessageBox MB_OK|MB_ICONSTOP "Another OkaySpace Setup or uninstall is already running.$\r$\n$\r$\nWait for it to finish, then try again." /SD IDOK
    ${LogU} "Exit code ${EXIT_BUSY}."
    SetErrorLevel ${EXIT_BUSY}
    Abort
  ${EndIf}
FunctionEnd

Function un.OptionsCreate
  !insertmacro MUI_HEADER_TEXT "Uninstall OkaySpace" "Choose what happens to your own files."
  nsDialogs::Create 1018
  Pop $R0
  ${If} $R0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 82u "OkaySpace will be removed from:$\r$\n  $INSTDIR$\r$\n$\r$\nRemoved: the programs, the editors on the Versions page (a folder that holds a project stays), logs, temporary files, your sign-in on this computer, the shortcuts and the entry in Settings > Apps.$\r$\n$\r$\nKept, unless you tick the box below: your projects, downloads, settings, achievements, local accounts and account data exports, the official game with its saves, and any file of yours that Setup did not install. The last page lists what was kept."
  Pop $R0
  ${NSD_CreateCheckbox} 0 88u 100% 12u "Also remove my projects, settings and sign-in"
  Pop $UnCheck
  ${NSD_CreateLabel} 11u 102u 96% 24u "This cannot be undone. Projects you keep outside the OkaySpace folder are never touched."
  Pop $R0
  GetDlgItem $R0 $HWNDPARENT 1
  SendMessage $R0 ${WM_SETTEXT} 0 "STR:Uninstall"
  ${If} $UnRemoveData == "1"
    ${NSD_Check} $UnCheck
  ${EndIf}
  nsDialogs::Show
FunctionEnd

Function un.OptionsLeave
  ${NSD_GetState} $UnCheck $R0
  ${If} $R0 == ${BST_CHECKED}
    MessageBox MB_YESNO|MB_ICONEXCLAMATION|MB_DEFBUTTON2 "This will also delete your projects, settings, downloads, the official game and your sign-in from the OkaySpace folder.$\r$\n$\r$\nIt cannot be undone. Delete them?" /SD IDNO IDYES yes
    Abort
    yes:
    StrCpy $UnRemoveData "1"
  ${Else}
    StrCpy $UnRemoveData ""
  ${EndIf}
FunctionEnd

Function un.FinishShow
  StrCpy $R0 "OkaySpace was removed from $INSTDIR.$\r$\n$UnKept"
  ${If} $UnKept == ""
    StrCpy $R0 "$R0$\r$\nNothing was kept: everything of OkaySpace was removed."
  ${EndIf}
  ${If} $UnKeptMore == "1"
    StrCpy $R0 "$R0$\r$\n(more in the log)"
  ${EndIf}
  StrCpy $R0 "$R0$\r$\n$\r$\nThe log is $LogFile"
  SendMessage $mui.FinishPage.Text ${WM_SETTEXT} 0 "STR:$R0"
FunctionEnd

; ---- the uninstall ----------------------------------------------------------------------------------------------------------------
Section "Uninstall"
  ${LogU} "Remove my projects, settings and sign-in: $UnRemoveData"
  StrCpy $UnKept ""
  StrCpy $UnKeptMore ""
  StrCpy $UnGone 0
  StrCpy $UnLocal "$LOCALAPPDATA\OkaySpace"

  ; 1. Nothing is removed while a program of ours is running.
  retry:
  Call un.ScanRunning
  ${If} $RunningList != ""
    ${LogU} "OkaySpace is running: $RunningList"
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OkaySpace is still running:$\r$\n$\r$\n$RunningList$\r$\n$\r$\nClose it, then click Retry." /SD IDCANCEL IDRETRY retry
    StrCpy $FailQuiet "1"
    !insertmacro UnDie ${EXIT_RUNNING} "OkaySpace is running ($RunningList). Close it and run the uninstaller again. Nothing was removed."
  ${EndIf}
  ${LogU} "OK: no OkaySpace program is running"

  ; 2. The programs: every file Setup installed (this build's list, and the list the last Setup wrote), then empty folders.
  !insertmacro EACH_DIST_FILE UN_DELETE_ONE
  Call un.DeleteListed
  !insertmacro EACH_DIST_DIR UN_RMDIR_ONE
  RMDir /r "$INSTDIR\${STAGE_NAME}"

  ; 3. What the programs made that is not the person's work: logs, state, update leftovers.
  Delete "$INSTDIR\okay_update.log"
  Delete "$INSTDIR\okay_selfupdate.state"
  Delete "$INSTDIR\OkaySpace.exe.old"
  Delete "$INSTDIR\OkaySpace.exe.new"
  Delete "$INSTDIR\OkaySpace.exe.failed"
  Delete "$INSTDIR\Tools\okay_update.log"
  Delete "$INSTDIR\Tools\okay_crash.txt"
  Delete "$INSTDIR\Tools\okay_crashlog.txt"
  Delete "$INSTDIR\Tools\okay_ipc_request.txt"
  Delete "$INSTDIR\Tools\okay_ipc_ack.txt"
  Delete "$INSTDIR\Tools\*.exe.old"
  Delete "$INSTDIR\Tools\*.exe.new"
  Delete "$INSTDIR\Tools\*.exe.failed"
  Delete "$INSTDIR\Tools\*.dll.old"
  Delete "$INSTDIR\Tools\*.dll.new"
  Delete "$INSTDIR\Tools\*.dll.failed"
  Call un.CleanTemp

  ; 4. The editor versions (program files, with their own crash logs and scratch folders). A folder that holds a project stays.
  ${If} ${FileExists} "$INSTDIR\Versions\*.*"
    StrCpy $UnSweepProjects "1"
    ${If} $UnRemoveData == "1"
      StrCpy $UnSweepProjects ""
    ${EndIf}
    StrCpy $R0 "$INSTDIR\Versions"
    Call un.Sweep
  ${EndIf}

  ; 5. The sign-in token always goes. Everything else in the roaming folder is the person's (accounts, server list) unless the box is ticked.
  Delete "$APPDATA\OkaySpace\session.txt"
  Delete "$INSTDIR\session.txt"
  ${If} ${FileExists} "$APPDATA\OkaySpace\ipc\*.*"
    StrCpy $UnSweepProjects ""
    StrCpy $R0 "$APPDATA\OkaySpace\ipc"
    Call un.Sweep
  ${EndIf}
  ${LogU} "Removed: the sign-in token and the game ipc folder"

  ; 6. The person's own data: kept, or removed when the box is ticked.
  ${If} $UnRemoveData == "1"
    StrCpy $UnSweepProjects ""
    Call un.RemoveUserData
  ${Else}
    Call un.KeepUserData
  ${EndIf}

  ; 7. Shortcuts, the Apps & features entry, the remembered folder.
  ClearErrors
  ReadRegDWORD $R0 HKCU "${APP_KEY}" "DesktopShortcut"
  ${If} $R0 == 1
    Delete "$DESKTOP\OkaySpace.lnk"
  ${EndIf}
  Delete "$SMPROGRAMS\OkaySpace\OkaySpace.lnk"
  Delete "$SMPROGRAMS\OkaySpace\Uninstall OkaySpace.lnk"
  RMDir "$SMPROGRAMS\OkaySpace"
  Delete "$SMPROGRAMS\OkaySpace.lnk"
  DeleteRegKey HKCU "${ARP_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"

  ; 8. The uninstaller itself, the list, and the folder when nothing is left in it.
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\Uninstall.exe.old"
  Delete "$INSTDIR\${MANIFEST_NAME}"
  Delete "$INSTDIR\${MANIFEST_NAME}.old"
  SetOutPath "$TEMP"                ; a folder that is the current folder cannot be removed
  RMDir "$INSTDIR"
  ${If} ${FileExists} "$INSTDIR\*.*"
    Push "$INSTDIR"
    Push "your settings and any files of yours that Setup did not install"
    Call un.Kept
    Call un.ListLeft
  ${EndIf}
  RMDir "$APPDATA\OkaySpace"
  ${If} ${FileExists} "$APPDATA\OkaySpace\*.*"
    Push "$APPDATA\OkaySpace"
    Push "your account data and server list"
    Call un.Kept
  ${EndIf}
  ${If} $UnLocal != $INSTDIR
    RMDir "$UnLocal"
    ${If} ${FileExists} "$UnLocal\*.*"
      Push "$UnLocal"
      Push "achievements and play time"
      Call un.Kept
    ${EndIf}
  ${EndIf}
  ${LogU} "Removed $UnGone files from the folders that were swept."
  ${LogU} "Uninstall finished. Exit code 0."   ; here and not in un.onUninstSuccess, which runs only when the Finish page is closed
SectionEnd

; Log what is left directly in the install folder.
Function un.ListLeft
  FindFirst $R1 $R2 "$INSTDIR\*.*"
  ${DoUntil} $R2 == ""
    ${If} $R2 != "."
    ${AndIf} $R2 != ".."
      ${LogU} "Left in the install folder: $R2"
    ${EndIf}
    FindNext $R1 $R2
  ${Loop}
  FindClose $R1
FunctionEnd

; Box off: say what stays.
Function un.KeepUserData
  ${If} ${FileExists} "$INSTDIR\Projects\*.*"
    Push "$INSTDIR\Projects"
    Push "your projects"
    Call un.Kept
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\Tools\Projects\*.*"
    Push "$INSTDIR\Tools\Projects"
    Push "your projects"
    Call un.Kept
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\community\*.*"
    Push "$INSTDIR\community"
    Push "your downloads"
    Call un.Kept
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\downloads\*.*"
    Push "$INSTDIR\downloads"
    Push "your downloads"
    Call un.Kept
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\Games\*.*"
    Push "$INSTDIR\Games"
    Push "the official game and anything it saved"
    Call un.Kept
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\exports\*.*"
    Push "$INSTDIR\exports"
    Push "your account data exports"
    Call un.Kept
  ${EndIf}
FunctionEnd

; Box ticked: remove the person's data too. Never anything outside the OkaySpace folders.
Function un.RemoveUserData
  StrCpy $R0 "$INSTDIR\Projects"
  Call un.SweepIfThere
  StrCpy $R0 "$INSTDIR\Tools\Projects"
  Call un.SweepIfThere
  StrCpy $R0 "$INSTDIR\community"
  Call un.SweepIfThere
  StrCpy $R0 "$INSTDIR\downloads"
  Call un.SweepIfThere
  StrCpy $R0 "$INSTDIR\Games"
  Call un.SweepIfThere
  StrCpy $R0 "$INSTDIR\exports"
  Call un.SweepIfThere
  ; settings, state, local accounts, achievements, sign-in details
  Delete "$INSTDIR\launcher.cfg"
  Delete "$INSTDIR\notifications.cfg"
  Delete "$INSTDIR\news.cfg"
  Delete "$INSTDIR\scheduled.cfg"
  Delete "$INSTDIR\servers.cfg"
  Delete "$INSTDIR\chat_seen_*.cfg"
  Delete "$INSTDIR\imgui.ini"
  Delete "$INSTDIR\okay_recent.txt"
  Delete "$INSTDIR\okay_settings.txt"
  Delete "$INSTDIR\okay_layouts.txt"
  Delete "$INSTDIR\okay_projectview.txt"
  Delete "$INSTDIR\okay_custom_actions.txt"
  Delete "$INSTDIR\achievements.txt"
  Delete "$INSTDIR\playtime.txt"
  Delete "$INSTDIR\profile_showcase.txt"
  Delete "$INSTDIR\account_server.txt"
  Delete "$INSTDIR\account_apikey.txt"
  Delete "$INSTDIR\password_reset_url.txt"
  Delete "$INSTDIR\accounts.db"
  Delete "$INSTDIR\Tools\plugins_disabled.txt"
  Delete "$INSTDIR\Tools\imgui.ini"
  Delete "$INSTDIR\Tools\okay_recent.txt"
  Delete "$INSTDIR\Tools\okay_settings.txt"
  Delete "$INSTDIR\Tools\okay_layouts.txt"
  Delete "$INSTDIR\Tools\okay_projectview.txt"
  Delete "$INSTDIR\Tools\okay_custom_actions.txt"
  Delete "$APPDATA\OkaySpace\accounts.db"
  Delete "$APPDATA\OkaySpace\servers.cfg"
  ${If} $UnLocal != $INSTDIR
    Delete "$UnLocal\achievements.txt"
    Delete "$UnLocal\playtime.txt"
    Delete "$UnLocal\profile_showcase.txt"
  ${EndIf}
  ${LogU} "Removed: projects, downloads, the official game, account data exports, settings, local accounts and achievements (the box was ticked)"
FunctionEnd

Function un.SweepIfThere
  ${If} ${FileExists} "$R0\*.*"
    Call un.Sweep
    ${If} $R1 == 1
      ${LogU} "WARNING: something in $R0 could not be removed"
    ${EndIf}
  ${EndIf}
FunctionEnd

Function un.onUninstFailed
  ${If} $FailCode == ""
    ${LogU} "FAILED: the uninstall stopped early."
    ${LogU} "Exit code 2."
  ${EndIf}
FunctionEnd

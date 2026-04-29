; EZStemz NSIS installer script
;
; Builds a Windows installer that drops EZStemz.exe + the bundled demucs
; model into Program Files and creates Start Menu / uninstaller entries.
;
; Driven by /D defines from scripts/release.ps1 and the Windows CI job:
;   /DEZSTEMZ_VERSION="0.2.0"
;   /DEZSTEMZ_BUILD_DIR="C:/path/to/build-release/ezstemz_artefacts/Release"
;   /DEZSTEMZ_OUTPUT="C:/path/to/dist/EZStemz-0.2.0-Setup.exe"

!ifndef EZSTEMZ_VERSION
    !define EZSTEMZ_VERSION "0.0.0-dev"
!endif

; NSIS requires a strict 4-component numeric version for VIProductVersion.
; Caller can pass /DEZSTEMZ_VERSION_NUMERIC=1.2.3.4; otherwise we fall back
; to 0.0.0.0 so the build still succeeds for tagged versions like
; "0.0.0-test".
!ifndef EZSTEMZ_VERSION_NUMERIC
    !define EZSTEMZ_VERSION_NUMERIC "0.0.0.0"
!endif

!ifndef EZSTEMZ_BUILD_DIR
    !error "Pass /DEZSTEMZ_BUILD_DIR=... pointing at the directory containing EZStemz.exe"
!endif

!ifndef EZSTEMZ_OUTPUT
    !define EZSTEMZ_OUTPUT "EZStemz-${EZSTEMZ_VERSION}-Setup.exe"
!endif

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

Name        "EZStemz ${EZSTEMZ_VERSION}"
OutFile     "${EZSTEMZ_OUTPUT}"
InstallDir  "$PROGRAMFILES64\EZStemz"
InstallDirRegKey HKLM "Software\EZStemz" "InstallDir"
RequestExecutionLevel admin

VIProductVersion                "${EZSTEMZ_VERSION_NUMERIC}"
VIAddVersionKey ProductName     "EZStemz"
VIAddVersionKey CompanyName     "ezstemz"
VIAddVersionKey LegalCopyright  "Copyright (c) ezstemz"
VIAddVersionKey FileDescription "EZStemz Installer"
VIAddVersionKey FileVersion     "${EZSTEMZ_VERSION}"
VIAddVersionKey ProductVersion  "${EZSTEMZ_VERSION}"

!include "MUI2.nsh"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "EZStemz" SecCore
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; All build artefacts (exe + bundled model + any DLLs JUCE produced).
    File /r "${EZSTEMZ_BUILD_DIR}\*.*"

    WriteRegStr HKLM "Software\EZStemz" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\EZStemz" "Version"    "${EZSTEMZ_VERSION}"

    ; Standard Add/Remove Programs entry.
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz" \
        "DisplayName"     "EZStemz"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz" \
        "DisplayVersion"  "${EZSTEMZ_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz" \
        "Publisher"       "ezstemz"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz" \
        "DisplayIcon"     "$INSTDIR\EZStemz.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz" \
        "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz" \
        "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz" \
        "NoRepair" 1

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\EZStemz"
    CreateShortCut  "$SMPROGRAMS\EZStemz\EZStemz.lnk"   "$INSTDIR\EZStemz.exe"
    CreateShortCut  "$SMPROGRAMS\EZStemz\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop shortcut" SecDesktop
    CreateShortCut "$DESKTOP\EZStemz.lnk" "$INSTDIR\EZStemz.exe"
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\EZStemz.lnk"
    Delete "$SMPROGRAMS\EZStemz\EZStemz.lnk"
    Delete "$SMPROGRAMS\EZStemz\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\EZStemz"

    RMDir /r "$INSTDIR"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EZStemz"
    DeleteRegKey HKLM "Software\EZStemz"
SectionEnd

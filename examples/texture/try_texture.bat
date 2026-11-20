@echo off
rem Try okay-texture, our own texture tool (mips, atlas, compress). Double-click this file.
rem Needs Tools\okay-texture.exe from the same download (this folder is examples\texture).
setlocal
set T=%~dp0..\..\Tools\okay-texture.exe
if not exist "%T%" (echo Cannot find "%T%" - run this from the folder you unzipped. & pause & exit /b 1)
cd /d "%~dp0"
if not exist out mkdir out
echo.
echo 1) Mip chain of the colour checker (filtered in linear light, Lanczos): out\checker_0.png ... checker_7.png
"%T%" mips checker_color.png out\checker --filter lanczos --srgb
echo.
echo 2) Same picture compressed to BC1 with mips: out\checker.dds
"%T%" compress checker_color.png out\checker.dds --format bc1 --srgb
"%T%" info out\checker.dds
echo.
echo 3) Fence with alpha coverage kept (BC3): out\fence.dds
"%T%" compress fence_cutout.png out\fence.dds --cutout
echo.
echo 4) Normal map (BC5, renormalised mips): out\bumps.dds
"%T%" compress bumps_normal.png out\bumps.dds --normal
echo.
echo 5) Atlas of three icons with 1 pixel padding and bleed: out\icons.png and out\icons.json
"%T%" atlas out\icons.png out\icons.json icon_a.png icon_b.png icon_c.png --pad 1 --bleed 1 --rotate
echo.
echo 6) Which mip the streaming planner keeps at 100 pixels on screen
"%T%" stream out\checker.dds 100
echo.
echo Done. Open the files in the out folder (any picture viewer opens the PNGs; DDS viewers open the .dds files).
pause

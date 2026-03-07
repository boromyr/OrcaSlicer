@echo off

rem ============================================================
rem  MERGE PR ORCASLICER - stato aggiornato al 2025-03-07
rem  MERGED   = unita nel repo ufficiale -> riga commentata
rem  open     = ancora aperta           -> attiva
rem  ============================================================

rem --- #11065 | open | Practical Flow Ratio Calibration Test ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11065/head
git merge FETCH_HEAD --no-edit
pause

rem --- #11015 | open | Feature: Retract amount after wipe ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11015/head
git merge FETCH_HEAD --no-edit
pause

rem --- #10565 | open | Extra perimeters on overhangs vs Bridging ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/10565/head
git merge FETCH_HEAD --no-edit
pause

rem --- #11255 | open | Bridge Line Width + Improve bridge density ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11255/head
git merge FETCH_HEAD --no-edit
pause

rem --- #11535 | open | Visible separators for UI ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11535/head
git merge FETCH_HEAD --no-edit
pause

rem --- #11819 | open | Fix fan speed-up for overhangs fails when "Only overhangs" is enabled ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11819/head
git merge FETCH_HEAD --no-edit
pause

@REM rem --- #11925 | open | Feature: Max Resolution and Deviation settings exposed for Arachne ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11925/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

rem --- #11786 | open | Add support for infill fan speed control ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11786/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12044 | open | Improve profile load speed by %30 with yyjson parser ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12044/head
git merge FETCH_HEAD --no-edit
pause

rem --- #11899 | open | Fix: Right Edge of G-code Viewer Legend Rows Not Interactive ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11899/head
git merge FETCH_HEAD --no-edit
pause

rem --- #11526 | open | Elephant foot compensation for solid layers ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11526/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12057 | open | Correct gap fill width bounds in outer-only and mixed-wall cases ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12057/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12087 | open | [Enhancement] Adjust seam placer parameters ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12087/head
git merge FETCH_HEAD --no-edit
pause

rem --- #11879 | open | QoL - variable layer height limit ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11879/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12089 | open | QoL: collapsible categories in "Compare Presets" ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12089/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12113 | open | Feature: Thumbnails with bed ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12113/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12614 | open | Tool Position window: improve reading of vertex info ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12614/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12151 | open | Port of PrusaSlicer "consistent surface cooling logic" ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12151/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12187 | open | Clean top layer with small items above ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12187/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12055 | open | ENH: Relative bridge direction + Align bridge/Ironing angles with model ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12055/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12109 | open | Feat: Add Tangential Sacrificial Bridging for counterbore holes ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12109/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12513 | open | UI fixes / improvements ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12513/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12568 | open | External bridge fix ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12568/head
git merge FETCH_HEAD --no-edit
pause

rem --- #12508 | open | QoL: Default values in tooltips for Percent, FloatOrPercent, String and Bool options ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12508/head
git merge FETCH_HEAD --no-edit
pause
D:\Users\PC\Documents\GitHub\OrcaSlicer\build_release_vs.bat
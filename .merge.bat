@echo off

rem ============================================================
rem  MERGE PR ORCASLICER - stato aggiornato al 2026-04-30
rem  MERGED   = unita nel repo ufficiale -> riga commentata
rem  CLOSED   = chiusa senza merge      -> riga commentata
rem  OPEN     = ancora aperta           -> attiva
rem ============================================================

echo --- #11535 ^| OPEN ^| Visible separators for UI ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11535/head
git merge FETCH_HEAD --no-edit
pause

echo --- #10565 ^| OPEN ^| Extra perimeters on overhangs vs Bridging ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/10565/head
git merge FETCH_HEAD --no-edit
pause

echo --- #11255 ^| OPEN ^| Bridge Line Width + Improve bridge density ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11255/head
git merge FETCH_HEAD --no-edit
pause

echo --- #11065 ^| OPEN ^| Practical Flow Ratio Calibration Test ---
git apply "D:\Users\PC\Documents\GitHub\praticalflow.patch" & git add "*.cpp" "*.hpp" "*.h" & git commit -m "Practical Flow Ratio Calibration Test [skip ci]"
pause

echo --- #11819 ^| OPEN ^| Fix fan speed-up for overhangs fails when "Only overhangs" is enabled ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11819/head
git merge FETCH_HEAD --no-edit -X theirs
pause

echo --- #12087 ^| OPEN ^| [Enhancement] Adjust seam placer parameters ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12087/head
git merge FETCH_HEAD --no-edit
pause

echo --- #11879 ^| OPEN ^| QoL - variable layer height limit ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11879/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12089 ^| OPEN ^| QoL: collapsible categories in "Compare Presets" ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12089/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12113 ^| OPEN ^| Feature: Thumbnails with bed ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12113/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12614 ^| OPEN ^| Tool Position window: improve reading of vertex info ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12614/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #12151 ^| OPEN ^| Port of PrusaSlicer "consistent surface cooling logic" ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12151/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #12187 ^| OPEN ^| Clean top layer with small items above ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12187/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12055 ^| OPEN ^| ENH: Relative bridge direction + Align bridge/Ironing angles with model ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12055/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12109 ^| OPEN ^| Feat: Add Tangential Sacrificial Bridging for counterbore holes ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12109/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12513 ^| OPEN ^| UI fixes / improvements ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12513/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12568 ^| OPEN ^| External bridge fix ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12568/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12564 ^| OPEN ^| Reduce Spiral Z generation segment density ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12564/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12881 ^| OPEN ^| Improve incremental compile speed on windows ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12881/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #12974 ^| OPEN ^| Add Precise Seam placement feature ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12974/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #11187 ^| OPEN ^| feat(viewer): Display travel distance and move count in G-code summary ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11187/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #11015 ^| OPEN ^| Feature: Retract amount after wipe ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11015/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #11899 ^| OPEN ^| Fix: Right Edge of G-code Viewer Legend Rows Not Interactive ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11899/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #12736 ^| OPEN ^| feat: Add Z Anti-Aliasing (ZAA) contouring support (updated) ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12736/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #13365 ^| OPEN ^| Fix iconic button sizes on widgets (paint modes and gcode viewer buttons) ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13365/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13360 ^| OPEN ^| FIX: context: fix the .gcode.3mf not shown issue ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13360/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13373 ^| OPEN ^| Add Align/Distribute objects on the print plate ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13373/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13390 ^| OPEN ^| Fix role-based fan speeds being lost on layer change ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13390/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13379 ^| OPEN ^| Add Optimized Gyroid infill (auto-tuned wavelength + amplitude) ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13379/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12433 ^| OPEN ^| Improve GCode Functions ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12433/head
git merge FETCH_HEAD --no-edit
pause

D:\Users\PC\Documents\GitHub\OrcaSlicer\build_release_vs.bat
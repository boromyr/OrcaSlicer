@echo off

rem ============================================================
rem  MERGE PR ORCASLICER - stato aggiornato al 2026-06-28
rem  MERGED   = unita nel repo ufficiale -> riga commentata
rem  CLOSED   = chiusa senza merge      -> riga commentata
rem  OPEN     = ancora aperta           -> attiva
rem ============================================================

echo --- #11535 ^| OPEN ^| Visible separators for UI ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11535/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #11255 ^| MERGED ^| Bridge Line Width + Improve bridge density ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11255/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #11065 ^| OPEN ^| Practical Flow Ratio Calibration Test ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11065/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #11819 ^| OPEN ^| Fix fan speed-up for overhangs fails when "Only overhangs" is enabled ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11819/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #12087 ^| OPEN ^| [Enhancement] Adjust seam placer parameters ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12087/head
git merge FETCH_HEAD --no-edit
pause

echo --- #11879 ^| OPEN ^| QoL - variable layer - height limit ---
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

@REM echo --- #12187 ^| OPEN ^| Clean top layer with small items above ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12187/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #12055 ^| MERGED ^| ENH: Relative bridge direction + Align bridge/Ironing angles with model ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12055/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #12109 ^| OPEN ^| Feat: Add Tangential Sacrificial Bridging for counterbore holes ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12109/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #12568 ^| MERGED ^| External bridge fix ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12568/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #12881 ^| MERGED ^| Improve incremental compile speed on windows ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12881/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #13373 ^| OPEN ^| Add Align/Distribute objects on the print plate ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13373/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #13704 ^| MERGED ^| Realistic View: Phong Shading + Ambient Oclusion + Cast Shadows ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13704/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #12433 ^| OPEN ^| Improve GCode Functions ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12433/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13824 ^| OPEN ^| Add 'brim layers' setting ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13824/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13557 ^| OPEN ^| Update to CGAL 6.1.1 ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13557/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13536 ^| OPEN ^| New boost library (1.91.0) ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13536/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13679 ^| OPEN ^| Scaling fixes ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13679/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13041 ^| OPEN ^| Update Cereal version to 1.3.2 ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13041/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #14086 ^| OPEN ^| feat: Volumetric Temperature Compensation (VTC) post-processing ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14086/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #14021 ^| OPEN ^| Homepage Fixes Improvements ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14021/head
git merge FETCH_HEAD --no-edit
pause

echo --- #14296 ^| OPEN ^| Top Surface Expansion ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14296/head
git merge FETCH_HEAD --no-edit
pause

echo --- #14246 ^| OPEN ^| feat: Wave Overhangs ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14246/head
git merge FETCH_HEAD --no-edit
pause

D:\Users\PC\Documents\GitHub\OrcaSlicer\build_release_vs.bat

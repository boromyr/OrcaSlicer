@echo off

rem ============================================================
rem  MERGE PR ORCASLICER - stato aggiornato al 2026-08-01
rem  MERGED   = unita nel repo ufficiale -> riga commentata
rem  CLOSED   = chiusa senza merge      -> riga commentata
rem  OPEN     = ancora aperta           -> attiva
rem ============================================================

@REM echo --- #13557 ^| OPEN ^| Update to CGAL 6.1.1 ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13557/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #13536 ^| OPEN ^| New boost library (1.91.0) ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13536/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #11065 ^| OPEN ^| Practical Flow Ratio Calibration Test ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11065/head
git merge FETCH_HEAD --no-edit
pause

echo --- #11535 ^| OPEN ^| Visible separators for UI ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11535/head
git merge FETCH_HEAD --no-edit
pause

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

echo --- #12109 ^| OPEN ^| Feat: Add Tangential Sacrificial Bridging for counterbore holes ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12109/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13373 ^| OPEN ^| Add Align/Distribute objects on the print plate ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13373/head
git merge FETCH_HEAD --no-edit
pause

echo --- #12433 ^| OPEN ^| Improve GCode Functions ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12433/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13824 ^| OPEN ^| Add 'brim layers' setting ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13824/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13679 ^| OPEN ^| Scaling fixes ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13679/head
git merge FETCH_HEAD --no-edit
pause

echo --- #14021 ^| OPEN ^| Homepage Fixes Improvements ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14021/head
git merge FETCH_HEAD --no-edit
pause

echo --- #14246 ^| OPEN ^| feat: Wave Overhangs ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14246/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #14689 ^| CLOSED ^| Reduce GPU load ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14689/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #14678 ^| MERGED ^| Support bugfixes ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14678/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #14787 ^| OPEN ^| WIP reconstruct context menu ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14787/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #14784 ^| MERGED ^| Cyclic ordering improvement ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14784/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #14928 ^| OPEN ^| Toggle settings for disabled features (skirt, brim, support) to show more of others tab ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14928/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #14678 ^| MERGED ^| Support bugfixes ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14678/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #14788 ^| MERGED ^| Fix overhang fan speed bugs ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14788/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

D:\Users\PC\Documents\GitHub\OrcaSlicer\build_release_vs.bat

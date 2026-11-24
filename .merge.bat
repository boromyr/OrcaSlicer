@echo off

rem ============================================================
rem  MERGE PR ORCASLICER - stato aggiornato al 2026-11-24
rem  MERGED   = unita nel repo ufficiale -> riga commentata
rem  CLOSED   = chiusa senza merge      -> riga commentata
rem  OPEN     = ancora aperta           -> attiva
rem ============================================================

@REM echo --- #13557 ^| CLOSED ^| Update to CGAL 6.1.1 ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13557/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #13536 ^| CLOSED ^| New boost library (1.91.0) ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13536/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

@REM echo --- #11065 ^| OPEN ^| Progressive (Practical) Flow Ratio Calibration Test ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11065/head
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

echo --- #12109 ^| OPEN ^| Feat: Add Tangential Sacrificial Bridging for counterbore holes ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/12109/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13824 ^| OPEN ^| Add 'brim layers' setting ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13824/head
git merge FETCH_HEAD --no-edit
pause

echo --- #14928 ^| OPEN ^| Toggle settings for disabled features (skirt, brim, support) to show more of others tab ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14928/head
git merge FETCH_HEAD --no-edit
pause

echo --- #15446 ^| OPEN ^| Feature: Overhang percentage and degree previews ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/15446/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #15495 ^| CLOSED ^| Refresh preview from post-processed G-code (BBL in-place path) ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/15495/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #14026 ^| OPEN ^| Add "Center/custom point" seam position ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14026/head
git merge FETCH_HEAD --no-edit
pause

echo --- #15702 ^| OPEN ^| Allow opening double clicked files in existing OrcaSlicer instance while also allowing to open a new instance ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/15702/head
git merge FETCH_HEAD --no-edit
git apply "D:\Users\PC\Documents\GitHub\instance.patch"
git commit -am "Always open G-code files in a new instance" --no-verify
pause

@REM echo --- #17561 ^| MERGED ^| Round mixed filament row corners ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17561/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #11535 ^| OPEN ^| Visible separators for UI ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/11535/head
git merge FETCH_HEAD --no-edit
pause

echo --- #17387 ^| OPEN ^| ENH: Add layer time smoothing and outer wall speed smoothing along Z ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17387/head
git merge FETCH_HEAD --no-edit
pause

echo --- #14246 ^| OPEN ^| feat: Wave Overhangs ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/14246/head
git merge FETCH_HEAD --no-edit
pause

echo --- #15603 ^| OPEN ^| Feature: Add interactive editor for small area flow compensation model ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/15603/head
git merge FETCH_HEAD --no-edit
pause

echo --- #15713 ^| OPEN ^| Master Eye ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/15713/head
git merge FETCH_HEAD --no-edit
pause

echo --- #15170 ^| OPEN ^| Feature: Add experimental Islands print sequencing ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/15170/head
git merge FETCH_HEAD --no-edit
pause

echo --- #13679 ^| OPEN ^| Scaling fixes ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/13679/head
git merge FETCH_HEAD --no-edit
pause

echo --- #17093 ^| OPEN ^| ENH: Add "Consistent surface" slowdown logic for layer cooling ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17093/head
git merge FETCH_HEAD --no-edit
pause

echo --- #16826 ^| OPEN ^| ENH: Add short travel acceleration and Consistent surface cooling logic for VFA reduction ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/16826/head
git merge FETCH_HEAD --no-edit
pause

@REM echo --- #17335 ^| MERGED ^| Speed up slicing without changing the G-code of a fresh slice ---
@REM git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17335/head
@REM git merge FETCH_HEAD --no-edit
@REM pause

echo --- #17513 ^| OPEN ^| fix: run post-processing in place after slicing for every printer profile ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17513/head
git merge FETCH_HEAD --no-edit
pause

echo --- #17026 ^| OPEN ^| Add ironing flow calibration ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17026/head
git merge FETCH_HEAD --no-edit
pause

echo --- #17611 ^| OPEN ^| Fix "Avoid crossing walls" skipped on the first travel of each layer ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17611/head
git merge FETCH_HEAD --no-edit
pause

echo --- #17696 ^| OPEN ^| «Preview workspace redesign: CanvasUi panels, new legend, rails and navigator cube ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17696/head
git merge FETCH_HEAD --no-edit
pause

echo --- #17412 ^| OPEN ^| External bridge fan control ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17412/head
git merge FETCH_HEAD --no-edit
pause

echo --- #17236 ^| OPEN ^| Optimize RSS RAM during idle and slicing pipelines, eliminate memory retention ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17236/head
git merge FETCH_HEAD --no-edit
pause

echo --- #17212 ^| OPEN ^| feat: Add optional extra wall overlap for Arachne overhangs ---
git fetch https://github.com/SoftFever/OrcaSlicer.git pull/17212/head
git merge FETCH_HEAD --no-edit
pause


D:\Users\PC\Documents\GitHub\OrcaSlicer\build_win.bat -s -l -x -i -j 22

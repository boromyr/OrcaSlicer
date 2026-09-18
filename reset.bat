@echo off
echo ------ RESET ------
git fetch upstream & git checkout main & git reset --hard upstream/main & git commit --amend -m "Reset [skip ci]" --no-edit & git push origin main --force & git apply "D:\Users\PC\Documents\GitHub\reset.patch"
echo ------ mod.patch ------
git apply "D:\Users\PC\Documents\GitHub\mod.patch"
echo ------ merge.patch ------
git apply "D:\Users\PC\Documents\GitHub\merge.patch"
echo ------ apa.patch ------
git apply "D:\Users\PC\Documents\GitHub\apa.patch"
echo ------ APAseam.patch ------
git apply "D:\Users\PC\Documents\GitHub\APAseam.patch"
echo ------ bridge.patch ------
git apply "D:\Users\PC\Documents\GitHub\bridge.patch"
echo ------ xy.patch ------
git apply "D:\Users\PC\Documents\GitHub\xy.patch"
echo ------ pa_flow.patch ------
git apply "D:\Users\PC\Documents\GitHub\pa_flow.patch"
echo ------ gyroid.patch ------
git apply "D:\Users\PC\Documents\GitHub\gyroid.patch"
echo ------ bridgetemp.patch ------
git apply "D:\Users\PC\Documents\GitHub\bridgetemp.patch"
echo ------ toolbar.patch ------
git apply "D:\Users\PC\Documents\GitHub\toolbar.patch"
echo ------ thumb.patch ------
git apply "D:\Users\PC\Documents\GitHub\thumb.patch"
echo ------ layer.patch ------
git apply "D:\Users\PC\Documents\GitHub\layer.patch"
echo ------ homepage.patch ------
git apply "D:\Users\PC\Documents\GitHub\homepage.patch"
echo ------ COMMIT ------
git add -A & git commit -m ". [skip ci]" & git push origin main --force
@REM git format-patch -1 --stdout > D:\Users\PC\Documents\GitHub\mod.patch

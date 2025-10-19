git fetch upstream & git checkout main & git reset --hard upstream/main & git clean -fdx & git push origin main --force & git apply D:\Users\PC\Documents\GitHub\0001-.patch & git add -A & git commit -m "."
@REM & xcopy D:\Users\PC\Desktop\.github\workflows\ D:\Users\PC\Documents\GitHub\OrcaSlicer\.github\workflows /s /e /y & git format-patch -1 --stdout > D:\Users\PC\Documents\GitHub\0001-.patch

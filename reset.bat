git fetch upstream
git checkout main
git reset --hard upstream/main
git clean -fdx
git push origin main --force
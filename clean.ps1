# Removes all printer-vendor profile folders from the build output except the
# ones listed in $Keep. Meant to run right after a build, e.g. from merge.bat.
# Targets the build output directory only - never the source tree under
# resources/profiles.

$Keep = @("Voron", "Custom", "OrcaFilamentLibrary")

$ProfilesDir = "D:\Users\PC\Documents\GitHub\OrcaSlicer\build-clang\OrcaSlicer\resources\profiles"

if (-not (Test-Path $ProfilesDir)) {
    Write-Error "Not found: $ProfilesDir"
    exit 1
}

$VendorDirs = Get-ChildItem -Path $ProfilesDir -Directory | Where-Object { $Keep -notcontains $_.Name }

Write-Host "Profiles directory: $ProfilesDir"
Write-Host "Keeping: $($Keep -join ', ')"
Write-Host ""

$TotalSize = 0
foreach ($dir in $VendorDirs) {
    $jsonFile = Join-Path $ProfilesDir "$($dir.Name).json"
    $size = (Get-ChildItem -Path $dir.FullName -Recurse -File | Measure-Object -Property Length -Sum).Sum
    $TotalSize += $size
    $sizeMB = [math]::Round($size / 1MB, 1)

    Remove-Item -Path $dir.FullName -Recurse -Force -Confirm:$false
    if (Test-Path $jsonFile) {
        Remove-Item -Path $jsonFile -Force -Confirm:$false
    }
    # Write-Host "Removed: $($dir.Name) ($sizeMB MB)"
}

$TotalMB = [math]::Round($TotalSize / 1MB, 1)
Write-Host ""
Write-Host "Total: $TotalMB MB across $($VendorDirs.Count) vendor folders removed from build output."

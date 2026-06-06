Write-Host "[1/4] Cleaning..."
Remove-Item -Recurse -Force build -ErrorAction Ignore
Remove-Item -Recurse -Force dist -ErrorAction Ignore

Write-Host "[2/4] Configuring..."
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

Write-Host "[3/4] Building..."
ninja -C build

Write-Host "[4/4] Packaging..."
New-Item -ItemType Directory -Force dist | Out-Null

Copy-Item build\Release\quark.exe dist\quark.exe

Write-Host "Done."
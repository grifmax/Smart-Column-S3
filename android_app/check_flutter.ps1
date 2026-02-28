# Flutter installation check script

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Flutter Installation Check" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check Flutter in PATH
Write-Host "Checking Flutter in PATH..." -ForegroundColor Yellow
try {
    $flutterVersion = flutter --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "OK Flutter found in PATH!" -ForegroundColor Green
        Write-Host ""
        Write-Host "Flutter version:" -ForegroundColor Cyan
        Write-Host $flutterVersion[0] -ForegroundColor Gray
        Write-Host ""
        
        Write-Host "Checking environment..." -ForegroundColor Yellow
        flutter doctor
        Write-Host ""
        
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "  OK Flutter is ready!" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
        exit 0
    }
} catch {
    # Flutter not found
}

Write-Host "ERROR Flutter not found in PATH" -ForegroundColor Red
Write-Host ""

# Search Flutter in common locations
Write-Host "Searching Flutter in common locations..." -ForegroundColor Yellow
$commonPaths = @(
    "C:\src\flutter",
    "C:\flutter",
    "$env:USERPROFILE\flutter",
    "$env:USERPROFILE\src\flutter",
    "$env:LOCALAPPDATA\flutter"
)

$foundFlutter = $null
foreach ($path in $commonPaths) {
    $flutterExe = Join-Path $path "bin\flutter.exe"
    if (Test-Path $flutterExe) {
        $foundFlutter = $path
        Write-Host "OK Found Flutter: $path" -ForegroundColor Green
        break
    }
}

if ($foundFlutter) {
    Write-Host ""
    Write-Host "Adding Flutter to PATH for current session..." -ForegroundColor Yellow
    $flutterBinPath = Join-Path $foundFlutter "bin"
    $env:Path += ";$flutterBinPath"
    
    Write-Host "Testing..." -ForegroundColor Yellow
    try {
        $flutterVersion = flutter --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "OK Flutter works!" -ForegroundColor Green
            Write-Host ""
            Write-Host "Flutter version:" -ForegroundColor Cyan
            Write-Host $flutterVersion[0] -ForegroundColor Gray
            Write-Host ""
            Write-Host "WARNING: This is only for current PowerShell session!" -ForegroundColor Yellow
            Write-Host "To make it permanent, add Flutter to PATH:" -ForegroundColor Yellow
            Write-Host "  1. Win + R -> sysdm.cpl" -ForegroundColor Gray
            Write-Host "  2. Advanced -> Environment Variables" -ForegroundColor Gray
            Write-Host "  3. Path -> Edit -> New" -ForegroundColor Gray
            $pathToAdd = Join-Path $foundFlutter "bin"
            Write-Host "  4. Add: $pathToAdd" -ForegroundColor Gray
            Write-Host "  5. Restart Cursor" -ForegroundColor Gray
        }
    } catch {
        Write-Host "ERROR checking Flutter" -ForegroundColor Red
    }
} else {
    Write-Host "ERROR Flutter not found in common locations" -ForegroundColor Red
    Write-Host ""
    Write-Host "Instructions:" -ForegroundColor Yellow
    Write-Host "1. Find the folder where you extracted Flutter" -ForegroundColor Gray
    Write-Host "2. Add flutter\bin to PATH:" -ForegroundColor Gray
    Write-Host "   - Win + R -> sysdm.cpl" -ForegroundColor Gray
    Write-Host "   - Advanced -> Environment Variables" -ForegroundColor Gray
    Write-Host "   - Path -> Edit -> New" -ForegroundColor Gray
    Write-Host "   - Add path (e.g.: C:\src\flutter\bin)" -ForegroundColor Gray
    Write-Host "3. Restart Cursor" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Or use full path:" -ForegroundColor Yellow
    Write-Host "  C:\path\to\flutter\bin\flutter.exe --version" -ForegroundColor Gray
}

Write-Host ""
Write-Host "To configure in Cursor:" -ForegroundColor Yellow
Write-Host "  Ctrl + Shift + P -> 'Flutter: Locate SDK'" -ForegroundColor Gray
Write-Host "  Specify path to Flutter folder (not bin, but flutter folder itself)" -ForegroundColor Gray

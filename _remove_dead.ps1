$f = Join-Path (Get-Location) "src\drivers\display.cpp"
$lines = Get-Content $f -Encoding UTF8
# Строки 4258-4313 (1-indexed) -> индексы 4257-4312 (0-indexed)
$keep = $lines[0..4256] + $lines[4313..($lines.Length - 1)]
$enc = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($f, $keep, $enc)
Write-Host "Done. Lines: $($keep.Count)"

# Builds 3DTube Music on Windows with devkitPro.
# GNU make can't handle spaces in the project path (e.g. "F:\3DS Homebrew\3DTube Music"), and MSYS resolves
# junctions back to the real path, so the project is temporarily mapped to a free drive letter with subst.
# Usage: .\build.ps1 [make arguments, e.g. clean]
$ErrorActionPreference = 'Stop'

$bash = $null
$make = Get-Command make.exe -ErrorAction SilentlyContinue
if ($make) { $bash = Join-Path (Split-Path $make.Source) 'bash.exe' }
if (-not $bash -or -not (Test-Path $bash)) { $bash = 'C:\devkitPro\msys2\usr\bin\bash.exe' }
if (-not (Test-Path $bash)) { throw "devkitPro msys2 bash.exe not found; install devkitPro or add its msys2\usr\bin to PATH" }

$root = $PSScriptRoot
$mapped = $null
if ($root -match ' ') {
	$used = (Get-PSDrive -PSProvider FileSystem).Name
	$letter = @('P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y') | Where-Object { $_ -notin $used } | Select-Object -First 1
	if (-not $letter) { throw "No free drive letter for subst" }
	subst.exe "${letter}:" "$root"
	$mapped = "${letter}:"
	$msysRoot = "/$($letter.ToLower())/"
} else {
	$msysRoot = & $bash -lc "cygpath -u '$root'"
}

try {
	& $bash -lc "cd '$msysRoot' && make $($args -join ' ')"
	if ($LASTEXITCODE -ne 0) { throw "make failed with exit code $LASTEXITCODE" }
} finally {
	if ($mapped) { subst.exe $mapped /D }
}

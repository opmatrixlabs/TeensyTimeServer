param(
  [string]$Compiler = 'g++'
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$sketch = Get-Content -LiteralPath (Join-Path $projectDirectory 'TeensyTimeServer.ino') -Raw
$harness = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'PpsTimestampTests.cpp') -Raw

# Extracts exactly one named production declaration or function from the sketch.
function Get-SketchFragment([string]$Pattern, [string]$Name) {
  $found = [regex]::Matches($sketch, $Pattern)
  if ($found.Count -ne 1) {
    throw "Expected exactly one production definition for $Name, found $($found.Count)."
  }
  return $found[0].Value
}

$declarations = @()
foreach ($name in @('TIMTP_STALE_MILLIS', 'TIME_PULSE_STALE_MICROS')) {
  $declarations += Get-SketchFragment "(?m)^constexpr\s+\w+\s+$name\s*=\s*[^;]+;" $name
}
$functions = @()
foreach ($name in @('getPpsTimestamp', 'readPpsTimestamp', 'getTimePulseStatus')) {
  $functions += Get-SketchFragment "(?ms)^(?:bool|void)\s+$name\s*\([^;{}]*\)\s*\{.*?^\}" $name
}
$source = $harness.Replace('// @PPS_TIMESTAMP_DECLARATIONS@', ($declarations -join "`n"))
$source = $source.Replace('// @PPS_TIMESTAMP_SOURCE@', ($functions -join "`n`n"))
$executable = Join-Path ([System.IO.Path]::GetTempPath()) ("TeensyPpsTimestampTests-" + [guid]::NewGuid().ToString('N') + '.exe')
try {
  $source | & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-pedantic' '-x' 'c++' '-' `
    (Join-Path $projectDirectory 'PpsClock.cpp') `
    (Join-Path $projectDirectory 'NtpTimestamp.cpp') `
    '-I' $projectDirectory '-o' $executable
  if ($LASTEXITCODE -ne 0) {
    throw "PPS timestamp integration tests did not compile (exit code $LASTEXITCODE)."
  }
  & $executable
  if ($LASTEXITCODE -ne 0) {
    throw "PPS timestamp integration tests failed (exit code $LASTEXITCODE)."
  }
}
finally {
  if (Test-Path -LiteralPath $executable) {
    Remove-Item -LiteralPath $executable
  }
}

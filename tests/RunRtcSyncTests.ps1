param(
  [string]$Compiler = 'g++'
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$sketch = Get-Content -LiteralPath (Join-Path $projectDirectory 'TeensyTimeServer.ino') -Raw
$harness = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'RtcSyncTests.cpp') -Raw

# Extracts exactly one declaration or function from the production sketch.
function Get-SketchFragment([string]$Pattern, [string]$Name) {
  $found = [regex]::Matches($sketch, $Pattern)
  if ($found.Count -ne 1) {
    throw "Expected exactly one production definition for $Name, found $($found.Count)."
  }
  return $found[0].Value
}

$declarations = @()
foreach ($name in @('RtcSyncState', 'RtcTimestampReadStatus')) {
  $declarations += Get-SketchFragment "(?ms)^enum class $name\b[^{}]*\{.*?^\};" $name
}
foreach ($name in @(
    'DEFAULT_RTC_WRITE_MICROS', 'RTC_CAPTURE_MAX_AGE_MICROS',
    'RTC_INITIALIZATION_RETRY_MILLIS', 'TIMTP_STALE_MILLIS',
    'RTC_SYNC_MAX_ATTEMPTS', 'RTC_SYNC_RETRY_MILLIS',
    'RTC_SYNC_VERIFY_READS', 'RTC_SYNC_VERIFY_TOLERANCE_HUNDREDTHS')) {
  $declarations += Get-SketchFragment "(?m)^constexpr\s+\w+\s+$name\s*=\s*[^;]+;" $name
}

$functions = @()
foreach ($name in @(
    'hasElapsed', 'rtcSyncIntervalExpired', 'setRtc', 'reportRtcSyncErrorOnce',
    'writeRtcAtCapturedPulse', 'verifyRtcWrite', 'serviceRtcSync')) {
  # Sketch functions use unindented closing braces, so nested blocks remain included.
  $functions += Get-SketchFragment "(?ms)^(?:bool|void|RtcTimestampReadStatus)\s+$name\s*\([^;{}]*\)\s*\{.*?^\}" $name
}

$source = $harness.Replace('// @RTC_SYNC_DECLARATIONS@', ($declarations -join "`n"))
$source = $source.Replace('// @RTC_SYNC_SOURCE@', ($functions -join "`n`n"))
$executable = Join-Path ([System.IO.Path]::GetTempPath()) ("TeensyRtcSyncTests-" + [guid]::NewGuid().ToString('N') + '.exe')

try {
  $source | & $Compiler '-std=c++17' '-Wall' '-Wextra' '-pedantic' '-x' 'c++' '-' `
    (Join-Path $projectDirectory 'TimeData.cpp') `
    (Join-Path $projectDirectory 'RtcTimestamp.cpp') `
    (Join-Path $projectDirectory 'PpsClock.cpp') `
    (Join-Path $projectDirectory 'NtpTimestamp.cpp') `
    '-I' (Join-Path $PSScriptRoot 'support') '-I' $projectDirectory '-o' $executable
  if ($LASTEXITCODE -ne 0) {
    throw "RTC synchronization tests did not compile (exit code $LASTEXITCODE)."
  }
  & $executable
  if ($LASTEXITCODE -ne 0) {
    throw "RTC synchronization tests failed (exit code $LASTEXITCODE)."
  }
}
finally {
  if (Test-Path -LiteralPath $executable) {
    Remove-Item -LiteralPath $executable
  }
}

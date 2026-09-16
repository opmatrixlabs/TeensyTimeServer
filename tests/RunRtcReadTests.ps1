param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$sketch = Get-Content -LiteralPath (Join-Path $projectDirectory 'TeensyTimeServer.ino') -Raw
$harness = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'RtcReadTests.cpp') -Raw
$fragments = @()
$patterns = @('(?ms)^enum class RtcTimestampReadStatus\b[^{}]*\{.*?^\};')
foreach ($name in @('discardRtcReceiveBuffer', 'readRtcRegisters', 'readRtcDateTime')) {
  $patterns += "(?ms)^(?:bool|void|RtcTimestampReadStatus)\s+$name\s*\([^;{}]*\)\s*\{.*?^\}"
}
foreach ($pattern in $patterns) {
  $found = [regex]::Matches($sketch, $pattern)
  if ($found.Count -ne 1) { throw "Expected one production match: $pattern" }
  $fragments += $found[0].Value
}
$source = $harness.Replace('// @RTC_READ_SOURCE@', ($fragments -join "`n`n"))
$outputDirectory = Join-Path $projectDirectory 'build\tests'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$executable = Join-Path $outputDirectory 'RtcReadTests.exe'
$source | & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-pedantic' '-x' 'c++' '-' `
  (Join-Path $projectDirectory 'RtcTimestamp.cpp') '-I' $projectDirectory '-o' $executable
if ($LASTEXITCODE -ne 0) { throw 'RTC read tests did not compile.' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'RTC read tests failed.' }

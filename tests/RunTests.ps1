param(
  [string]$Compiler = 'g++'
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$dependencies = @{
  ClockPrecisionTests = @('ClockPrecision.cpp')
  FirmwareImageValidatorTests = @('FirmwareImageValidator.cpp', 'IntelHexParser.cpp')
  GnssStatusTests = @('GnssStatus.cpp')
  IntelHexParserTests = @('IntelHexParser.cpp')
  NtpPacketTests = @('NtpPacket.cpp', 'NtpTimestamp.cpp')
  NtpTimestampTests = @('NtpTimestamp.cpp')
  PpsCaptureTests = @('PpsCapture.cpp')
  PpsClockTests = @('PpsClock.cpp', 'NtpTimestamp.cpp')
  RtcTimestampTests = @('RtcTimestamp.cpp')
  TimeDataTests = @('TimeData.cpp')
}

$testFiles = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*Tests.cpp' -File | Sort-Object Name)
if ($testFiles.Count -eq 0) {
  throw 'No host tests found.'
}

$passed = 0
foreach ($test in $testFiles) {
  if ($test.BaseName -in @('RtcSyncTests', 'PpsTimestampTests', 'RtcReadTests')) {
    & (Join-Path $PSScriptRoot ('Run' + $test.BaseName + '.ps1')) -Compiler $Compiler
  }
  else {
    if (!$dependencies.ContainsKey($test.BaseName)) {
      throw "Add production dependencies for $($test.Name) to RunTests.ps1."
    }

    $executable = Join-Path ([System.IO.Path]::GetTempPath()) (
      'Teensy-' + $test.BaseName + '-' + [guid]::NewGuid().ToString('N') + '.exe')
    try {
      $arguments = @('-std=c++17', '-Wall', '-Wextra', '-pedantic', $test.FullName)
      if ($test.BaseName -eq 'PpsCaptureTests') {
        $arguments += '-DPPS_CAPTURE_MATH_TEST'
      }
      foreach ($source in $dependencies[$test.BaseName]) {
        $arguments += Join-Path $projectDirectory $source
      }
      $arguments += @('-I', (Join-Path $PSScriptRoot 'support'), '-I', $projectDirectory, '-o', $executable)
      & $Compiler @arguments
      if ($LASTEXITCODE -ne 0) {
        throw "$($test.Name) did not compile (exit code $LASTEXITCODE)."
      }
      & $executable
      if ($LASTEXITCODE -ne 0) {
        throw "$($test.Name) failed (exit code $LASTEXITCODE)."
      }
    }
    finally {
      if (Test-Path -LiteralPath $executable) {
        Remove-Item -LiteralPath $executable
      }
    }
  }
  ++$passed
  Write-Output "$($test.BaseName): PASS"
}

Write-Output "All $passed host test suites passed."

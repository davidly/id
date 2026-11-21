#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$command = Join-Path $PSScriptRoot '..\check-synthid.ps1'
$imagePath = Join-Path $PSScriptRoot 'images\plain.jpg'
$expectedBytes = [Convert]::ToBase64String([IO.File]::ReadAllBytes($imagePath))
$mock = @{ Calls = 0; Outcome = 'detected' }

function Invoke-RestMethod {
    param($Method, $Uri, $Authentication, $Token, $Body, $MaximumRedirection, $TimeoutSec)
    $mock.Calls++
    $parts = @($Body)
    if ($Uri -ne 'https://api.openai.com/v1/content_provenance_checks' -or $Method -ne 'Post' -or
        $Authentication -ne 'Bearer' -or $Token -isnot [Security.SecureString] -or
        $MaximumRedirection -ne 0 -or $TimeoutSec -ne 60 -or $parts.Count -ne 1) {
        throw 'Unexpected upload request.'
    }
    $part = $parts[0]
    if ($part.Headers.ContentType.MediaType -ne 'image/jpeg' -or
        $part.Headers.ContentDisposition.Name.Trim('"') -ne 'file' -or
        $part.Headers.ContentDisposition.FileName.Trim('"') -ne 'image.jpg' -or
        [Convert]::ToBase64String($part.ReadAsByteArrayAsync().GetAwaiter().GetResult()) -cne $expectedBytes) {
        throw 'Unexpected contents of the multipart file.'
    }
    @{
        object = 'content_provenance_check'
        results = @(
            @{ type = 'c2pa'; outcome = 'detected' }
            @{ type = 'synthid'; outcome = $mock.Outcome; model = 'fixture-model'; generated_at = $null }
        )
    }
}

$previousKey = $env:OPENAI_API_KEY
try {
    $env:OPENAI_API_KEY = 'offline-test-key'
    $blocked = $false
    try { & $command -Image $imagePath | Out-Null }
    catch { $blocked = $_.Exception.Message -like 'Pass -SendToOpenAI*' }
    if (-not $blocked -or $mock.Calls) { throw 'Missing opt-in did not block the request.' }

    & $command -Image $imagePath -SendToOpenAI -WhatIf | Out-Null
    if ($mock.Calls) { throw 'WhatIf sent a request.' }

    $detected = & $command -Image $imagePath -SendToOpenAI
    if ($detected.Outcome -ne 'detected' -or $detected.Model -ne 'fixture-model') {
        throw 'The detected outcome was not returned.'
    }

    $mock.Outcome = 'not_detected'
    $absent = & $command -Image $imagePath -SendToOpenAI
    if ($absent.Outcome -ne 'not_detected' -or $absent.Result -notlike '*inconclusive') {
        throw 'The negative outcome should remain inconclusive, even if the C2PA result is positive.'
    }

    $mock.Outcome = 'unexpected'
    $rejected = $false
    try { & $command -Image $imagePath -SendToOpenAI | Out-Null }
    catch { $rejected = $_.Exception.Message -like '*did not contain a recognized SynthID result*' }
    if (-not $rejected -or $mock.Calls -ne 3) { throw 'Unexpected provider responses should fail clearly.' }

    Write-Host 'PASS SynthID opt-in, multipart upload contract, and response handling (offline)'
}
finally {
    $env:OPENAI_API_KEY = $previousKey
}

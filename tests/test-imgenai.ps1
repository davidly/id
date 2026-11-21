[CmdletBinding()]
param(
    [string] $Exe,
    [switch] $RequireC2pa
)

$ErrorActionPreference = 'Stop'
if (-not $Exe) { $Exe = Join-Path $PSScriptRoot '..\imgenai.exe' }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$images = Join-Path $PSScriptRoot 'images'
$work = Join-Path ([IO.Path]::GetTempPath()) ('imgenai-tests-' + [guid]::NewGuid())
[IO.Directory]::CreateDirectory($work) | Out-Null

function Invoke-Imgenai([string[]] $Arguments, [int] $ExpectedExit = 0) {
    $lines = @(& $Exe @Arguments)
    if ($LASTEXITCODE -ne $ExpectedExit) {
        throw "Expected exit $ExpectedExit, got $LASTEXITCODE for $Arguments.`n$($lines -join "`n")"
    }
    return ($lines -join "`n")
}

function Assert-Contains([string] $Output, [string] $Expected, [string] $Case) {
    if (-not $Output.Contains($Expected)) {
        throw "$Case expected '$Expected'.`n$Output"
    }
}

$declared = 'Metadata declares generative AI use.'
$editor = 'Metadata records an AI-capable editor.'
$unknown = 'Metadata cannot determine whether generative AI was used.'
$credential = 'C2PA/JUMBF data block detected'
$c2paSourceType = 'C2PA manifest (signature not verified): digitalSourceType trainedAlgorithmicMedia'
$cases = @(
    @{ File = 'plain.jpg'; Result = $unknown; Copy = 'same' }
    @{ File = 'firefly-caption.jpg'; Result = $unknown; Copy = 'same' }
    @{ File = 'generic-algorithmic.jpg'; Result = $unknown; Evidence = '/algorithmicMedia'; Copy = 'same' }
    @{ File = 'wrong-namespace.jpg'; Result = $unknown; Copy = 'same' }
    @{ File = 'camera-raw-editor.jpg'; Result = $editor; Evidence = 'Adobe Camera Raw'; Copy = 'same' }
    @{ File = 'generative-composite.jpg'; Result = $declared; Evidence = '/compositeWithTrainedAlgorithmicMedia'; Copy = 'plain' }
    @{ File = 'iptc-ai-fields.jpg'; Result = $declared; Evidence = 'IPTC AI System Version Used'; Copy = 'stripped' }
    @{ File = 'c2pa-only-generator.jpg'; Result = $declared; Evidence = $c2paSourceType; Copy = 'stripped' }
    @{ File = 'c2pa-split-generator.jpg'; Result = $declared; Evidence = $c2paSourceType; Copy = 'stripped' }
    @{ File = 'extended-xmp.jpg'; Result = $declared; Evidence = '/trainedAlgorithmicMedia'; Copy = 'plain' }
    @{ File = 'extended-xmp-unreferenced.jpg'; Result = $unknown; Copy = 'same' }
    @{ File = 'extended-xmp-out-of-order.jpg'; Result = $declared; Evidence = '/trainedAlgorithmicMedia'; Copy = 'plain' }
    @{ File = 'extended-xmp-bad-digest.jpg'; Result = $unknown; Copy = 'same' }
    @{ File = 'extended-xmp-overlap.jpg'; Result = $unknown; Copy = 'same' }
    @{ File = 'png-dream-caption.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'png-job-caption.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'comfyui-resize-only.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'provenance/capture.jpg'; Result = $unknown; Evidence = $credential; Copy = 'stripped' }
    @{ File = 'a1111-parameters.png'; Result = $declared; Evidence = 'Stable Diffusion WebUI generation settings'; Copy = 'none' }
    @{ File = 'comfyui-prompt.png'; Result = $declared; Evidence = 'ComfyUI prompt graph'; Copy = 'none' }
    @{ File = 'comfyui-detached-sampler.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'comfyui-mixed-outputs.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'comfyui-control-link.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'comfyui-zero-denoise.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'comfyui-no-save.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'comfyui-workflow-only.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'diffusion-json-placeholder.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'diffusion-json-nested.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'invokeai-ztxt.png'; Result = $declared; Evidence = 'InvokeAI generation settings'; Copy = 'none' }
    @{ File = 'novelai-software.png'; Result = $declared; Evidence = 'names NovelAI'; Copy = 'none' }
    @{ File = 'novelai-alpha.png'; Result = $declared; Evidence = 'NovelAI hidden metadata (signature not verified)'; Copy = 'none' }
    @{ File = 'png-editor-software.png'; Result = $editor; Evidence = 'PNG Software: Adobe Photoshop'; Copy = 'none' }
    @{ File = 'png-ordinary-text.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'png-ordinary-alpha.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'novelai-alpha-bad-crc.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'novelai-alpha-short-length.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'png-alpha-other-software.png'; Result = $unknown; Copy = 'none' }
    @{ File = 'png-cabx-generator.png'; Result = $declared; Evidence = $c2paSourceType; Copy = 'none' }
    @{ File = 'webp-xmp.webp'; Result = $declared; Evidence = '/trainedAlgorithmicMedia'; Copy = 'none' }
    @{ File = 'webp-c2pa-generator.webp'; Result = $declared; Evidence = $c2paSourceType; Copy = 'none' }
    @{ File = 'avif-xmp.avif'; Result = $declared; Evidence = '/trainedAlgorithmicMedia'; Copy = 'none' }
    @{ File = 'jxl-xmp.jxl'; Result = $declared; Evidence = '/trainedAlgorithmicMedia'; Copy = 'none' }
    @{ File = 'a1111-usercomment.jpg'; Result = $declared; Evidence = 'EXIF UserComment: Stable Diffusion WebUI'; Copy = 'same' }
    @{ File = 'ordinary-usercomment.jpg'; Result = $unknown; Copy = 'same' }
    @{ File = 'lightroom-classic-10.jpg'; Result = $unknown; Evidence = 'Editor version predates its generative features'; Copy = 'same' }
    @{ File = 'lightroom-classic-13.jpg'; Result = $editor; Evidence = 'Lightroom Classic 13.3'; Copy = 'same' }
    @{ File = 'photoshop-cs4.jpg'; Result = $unknown; Evidence = 'Editor version predates its generative features'; Copy = 'same' }
    @{ File = 'canvas-exporter.jpg'; Result = $unknown; Copy = 'same' }
    @{ File = 'midjourney-creatortool.jpg'; Result = $declared; Evidence = 'CreatorTool names Midjourney'; Copy = 'plain' }
    @{ File = 'https-source-type.jpg'; Result = $declared; Evidence = 'https://cv.iptc.org/'; Copy = 'plain' }
    @{ File = 'google-edited.jpg'; Result = $declared; Evidence = 'Edited with Google AI'; Copy = 'plain' }
    @{ File = 'alt-wrapped-ai-field.jpg'; Result = $declared; Evidence = 'IPTC AI Prompt Information'; Copy = 'plain' }
    @{ File = 'malformed-xmp-partial.jpg'; Result = $declared; Evidence = '/trainedAlgorithmicMedia'; Copy = 'plain' }
    @{ File = 'malformed-xmp-mention.jpg'; Result = $unknown; Evidence = 'Malformed XMP mentions: trainedAlgorithmicMedia'; Copy = 'plain' }
    @{ File = 'remote-manifest.jpg'; Result = $unknown; Evidence = 'Remote Content Credentials referenced'; Copy = 'same' }
)

$corpus = @(
# string (or '' for none). See the corresponding image directories for attribution.
    ,@('c2pa-org/nikon-20221019-building.jpeg', $unknown, $credential)
    ,@('c2pa-org/truepic-20230212-camera.jpg', $unknown, $credential)
    ,@('wikimedia/bentley-photoshop-1.jpg', $editor, 'Adobe Photoshop 26.1')
    ,@('wikimedia/bentley-firefly-1a.jpg', $declared, 'digitalSourceType compositeWithTrainedAlgorithmicMedia')
)

try {
    $plainHash = (Get-FileHash -LiteralPath (Join-Path $images 'plain.jpg')).Hash
    foreach ($case in $cases) {
        $source = Join-Path $images $case.File
        $destinationBase = Join-Path $work ([IO.Path]::GetFileNameWithoutExtension($source))
        $destination = $destinationBase + [IO.Path]::GetExtension($source)
        $sourceHash = (Get-FileHash -LiteralPath $source).Hash

        $assessment = Invoke-Imgenai @($source)
        Assert-Contains $assessment "RESULT: $($case.Result)" $case.File
        if ($case.Evidence) {
            Assert-Contains $assessment $case.Evidence $case.File
        }
        if ($case.Absent -and $assessment.Contains($case.Absent)) {
            throw "$($case.File) unexpectedly contains '$($case.Absent)'."
        }
        if ($case.Copy -eq 'none') {
            Write-Host "PASS $($case.File)"
            continue
        }

        $strip = Invoke-Imgenai @("-s:$destinationBase", $source)
        if ($case.File -eq 'a1111-usercomment.jpg') {
            Assert-Contains $strip 'Retained evidence: EXIF UserComment: Stable Diffusion WebUI' $case.File
            Assert-Contains (Invoke-Imgenai @($destination)) "RESULT: $declared" $case.File
        }
        $outputHash = (Get-FileHash -LiteralPath $destination).Hash
        if ($case.Copy -eq 'same' -and $outputHash -ne $sourceHash) {
            throw "$($case.File) should have been copied byte for byte."
        }
        if ($case.Copy -eq 'plain' -and $outputHash -ne $plainHash) {
            throw "$($case.File) should match the baseline after stripping its added XMP."
        }
        if ($case.Copy -ne 'same') {
            $clean = Invoke-Imgenai @($destination)
            Assert-Contains $clean "RESULT: $unknown" $case.File
            if ($outputHash -eq $sourceHash -or $clean.Contains($credential)) {
                throw "$($case.File) still has the original bytes or reports embedded C2PA."
            }
        }

        $null = Invoke-Imgenai @("-s:$destinationBase", $source) 2
        if ((Get-FileHash -LiteralPath $destination).Hash -ne $outputHash -or
            (Get-FileHash -LiteralPath $source).Hash -ne $sourceHash) {
            throw "$($case.File) source or existing destination changed."
        }
        Write-Host "PASS $($case.File)"
    }

    foreach ($entry in $corpus) {
        $file, $result, $evidence = $entry
        $assessment = Invoke-Imgenai @((Join-Path $images $file))
        Assert-Contains $assessment "RESULT: $result" $file
        if ($evidence) {
            Assert-Contains $assessment $evidence $file
        }
        Write-Host "PASS $file"
    }

    $verifier = Join-Path (Split-Path -Parent $Exe) 'c2patool.exe'
    if (Test-Path -LiteralPath $verifier -PathType Leaf) {
        $settings = Join-Path $work 'offline settings.json'
        '{"verify":{"verify_trust":false,"remote_manifest_fetch":false}}' |
            Set-Content -LiteralPath $settings -Encoding ascii
        $report = Invoke-Imgenai @("-c:$settings", (Join-Path $images 'provenance/capture.jpg'))
        $parts = $report -split '(?m)^C2PA validation report for the input image:\n', 2
        if ($parts.Count -ne 2) { throw "Missing C2PA report.`n$report" }
        $json = $parts[1] | ConvertFrom-Json
        $success = $json.validation_results.activeManifest.success.code
        if ($json.validation_state -ne 'Valid' -or
            'claimSignature.validated' -notin $success -or
            'assertion.dataHash.match' -notin $success) {
            throw "Expected a valid fixture signature and asset hash.`n$report"
        }
        Write-Host 'PASS C2PA signature and asset binding'

        $provenance = Join-Path $images 'provenance'
        $capture = Join-Path $provenance 'capture.jpg'
        Assert-Contains $report 'C2PA RESULT: Valid signature and asset binding; signer trust not established' 'untrusted capture'

        $anchors = [IO.File]::ReadAllText((Join-Path $provenance 'test-certificates.pem'))
        @{
            verify = @{ verify_trust = $true; remote_manifest_fetch = $false }
            trust = @{ anchors = @(@{ trust_anchors = $anchors; trust_kind = 'manifest' }) }
        } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $settings -Encoding ascii

        $builtinCases = @(
            @{ Path = 'plain.jpg'; Result = 'No credential found' }
            @{ Path = 'provenance/capture.jpg'; Result = 'Trusted capture claim bound to the current asset' }
            @{ Path = 'provenance/capture-changed.jpg'; Result = 'Invalid or incompletely validated credential' }
            @{ Path = 'provenance/generated.jpg'; Result = 'Trusted credential records AI generation' }
            @{ Path = 'provenance/generative-edit.jpg'; Result = 'Trusted credential records a generative edit' }
            @{ Path = 'provenance/generative-ingredient.jpg'; Result = 'Trusted credential records a generative ingredient' }
        )
        foreach ($case in $builtinCases) {
            $output = Invoke-Imgenai @("-c:$settings", '-q', (Join-Path $images $case.Path))
            Assert-Contains $output "C2PA RESULT: $($case.Result)" $case.Path
            if ($output.Contains('C2PA validation report for the input image:')) {
                throw "Unexpected raw report in summary-only mode for $($case.Path)."
            }
            Write-Host "PASS native C2PA summary $($case.Path)"
        }


        $invalidIngredient = Invoke-Imgenai @("-c:$settings", (Join-Path $provenance 'ingredient-invalid.jpg'))
        Assert-Contains $invalidIngredient 'assertion.dataHash.mismatch' 'invalid ingredient binding'
        Assert-Contains $invalidIngredient 'C2PA RESULT: Trusted credential without an explicit generative or capture-only claim' 'invalid ingredient conclusion'
        Write-Host 'PASS historical ingredient hash failure blocks a trusted generative finding'

        $sidecar = Join-Path $work 'explicit manifest.c2pa'
        Copy-Item -LiteralPath (Join-Path $provenance 'plain-detached.c2pa') -Destination $sidecar
        $plain = Join-Path $images 'plain.jpg'
        $detached = Invoke-Imgenai @("-c:$settings", "-m:$sidecar", '-q', $plain)
        Assert-Contains $detached 'C2PA RESULT: Trusted capture claim bound to the current asset' 'detached C2PA'
        Assert-Contains $detached 'C2PA source: explicitly supplied detached manifest' 'detached C2PA source'
        $embeddedOverride = Invoke-Imgenai @("-c:$settings", "-m:$sidecar", '-q', $capture)
        Assert-Contains $embeddedOverride 'C2PA RESULT: Invalid or incompletely validated credential' 'no embedded fallback'
        $mismatch = Invoke-Imgenai @("-c:$settings", "-m:$sidecar", '-q', (Join-Path $images 'ordinary-usercomment.jpg'))
        Assert-Contains $mismatch 'C2PA RESULT: Invalid or incompletely validated credential' 'wrong sidecar asset'
        $missing = Invoke-Imgenai @("-c:$settings", "-m:$sidecar.missing", '-q', $plain) 2
        Assert-Contains $missing 'The detached C2PA manifest file was not found.' 'missing sidecar'
        $null = Invoke-Imgenai @("-m:$sidecar", $plain) 1
        Write-Host 'PASS detached manifest validation and asset mismatch'
    }
    elseif ($RequireC2pa) {
        throw "c2patool.exe is missing next to $Exe."
    }
    else {
        Write-Host 'SKIP C2PA verification (c2patool.exe is missing)'
    }
}
finally {
    Get-ChildItem -LiteralPath $work -File | Remove-Item
    [IO.Directory]::Delete($work)
}

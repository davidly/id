param([Parameter(Mandatory)][string] $C2paTool)
$ErrorActionPreference = 'Stop'
$images = Join-Path $PSScriptRoot 'images'
$provenance = Join-Path $images 'provenance'
[IO.Directory]::CreateDirectory($provenance) | Out-Null

Add-Type -AssemblyName System.Drawing
$bitmap = New-Object Drawing.Bitmap(48, 48)
try {
    for ($y = 0; $y -lt 48; $y++) {
        for ($x = 0; $x -lt 48; $x++) {
            $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb(32 + 4*$x, 32 + 4*$y, 120))
        }
    }
    $memory = [IO.MemoryStream]::new()
    try {
        $bitmap.Save($memory, [Drawing.Imaging.ImageFormat]::Jpeg)
        [IO.File]::WriteAllBytes((Join-Path $images 'plain.jpg'), $memory.ToArray())
    } finally { $memory.Dispose() }
} finally { $bitmap.Dispose() }

& python (Join-Path $PSScriptRoot 'make-fixtures.py')
if ($LASTEXITCODE) { throw 'Fixture generation failed.' }
$C2paTool = (Resolve-Path -LiteralPath $C2paTool).Path
$plain = Join-Path $images 'plain.jpg'
$capture = Join-Path $provenance 'capture.jpg'
$generation = Join-Path $provenance 'generated.jpg'
$edit = Join-Path $provenance 'generative-edit.jpg'
$ingredient = Join-Path $provenance 'generative-ingredient.jpg'
$work = Join-Path ([IO.Path]::GetTempPath()) ('id-test-sign-' + [guid]::NewGuid())
[IO.Directory]::CreateDirectory($work) | Out-Null

function Sign-Fixture([string[]] $Arguments) {
    & $C2paTool @Arguments | Out-Null
    if ($LASTEXITCODE) { throw "c2patool failed: $Arguments" }
}

function Add-PostSigningComment([string] $InputFile, [string] $OutputFile) {
    $bytes = [IO.File]::ReadAllBytes($InputFile)
    $comment = [Text.Encoding]::ASCII.GetBytes('Added after signing')
    $marker = [byte[]]@(255, 254, 0, ($comment.Length + 2))
    [IO.File]::WriteAllBytes($OutputFile,
        [byte[]]($bytes[0..1] + $marker + $comment + $bytes[2..($bytes.Length - 1)]))
}

try {
    $plainDefinition = Join-Path $work 'capture.json'
    $editDefinition = Join-Path $work 'edit.json'
    '{"title":"OpenAI conference photo"}' | Set-Content -LiteralPath $plainDefinition -Encoding ascii
    @{
        title = 'Locally generated edit fixture'
        assertions = @(@{label='c2pa.actions.v2'; data=@{actions=@(@{
            action='c2pa.edited'
            digitalSourceType='http://cv.iptc.org/newscodes/digitalsourcetype/compositeWithTrainedAlgorithmicMedia'
        })}})
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $editDefinition -Encoding ascii
    Sign-Fixture @($plain, '-m', $plainDefinition, '--create', 'digitalCapture', '-f', '-o', $capture)
    Sign-Fixture @($plain, '-m', $plainDefinition, '--create', 'trainedAlgorithmicMedia', '-f', '-o', $generation)
    Sign-Fixture @($plain, '-m', $editDefinition, '--parent', $capture, '-f', '-o', $edit)
    Sign-Fixture @($plain, '-m', $plainDefinition, '--parent', $generation, '-f', '-o', $ingredient)
    $changedIngredient = Join-Path $work 'changed-ingredient.jpg'
    Add-PostSigningComment $generation $changedIngredient
    Sign-Fixture @($plain, '-m', $plainDefinition, '--parent', $changedIngredient, '-f',
                   '-o', (Join-Path $provenance 'ingredient-invalid.jpg'))
    $asset = Join-Path $work 'plain-detached.jpg'
    Sign-Fixture @($plain, '-m', $plainDefinition, '--create', 'digitalCapture', '--sidecar', '-o', $asset)
    if ((Get-FileHash -LiteralPath $plain).Hash -ne (Get-FileHash -LiteralPath $asset).Hash) { throw 'Detached asset bytes differ.' }
    Copy-Item -LiteralPath (Join-Path $work 'plain-detached.c2pa') -Destination $provenance -Force
    $certificates = @(& $C2paTool $capture --certs)
    if ($LASTEXITCODE) { throw 'Certificate extraction failed.' }
    ($certificates -join [Environment]::NewLine).TrimEnd() |
        Set-Content -LiteralPath (Join-Path $provenance 'test-certificates.pem') -Encoding ascii
    Add-PostSigningComment $capture (Join-Path $provenance 'capture-changed.jpg')
} finally {
    Get-ChildItem -LiteralPath $work -File | Remove-Item
    [IO.Directory]::Delete($work)
}

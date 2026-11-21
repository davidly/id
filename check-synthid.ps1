#Requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][string] $Image,
    [switch] $SendToOpenAI
)

$ErrorActionPreference = 'Stop'
if (-not $SendToOpenAI) {
    throw 'Pass -SendToOpenAI to send the selected image bytes to the OpenAI Content Provenance API.'
}

$file = Get-Item -LiteralPath $Image
if ($file -isnot [IO.FileInfo]) {
    throw 'Choose a local image file.'
}
$extension = $file.Extension.ToLowerInvariant()
$mediaType = @{
    '.png' = 'image/png'
    '.jpg' = 'image/jpeg'
    '.jpeg' = 'image/jpeg'
    '.webp' = 'image/webp'
    '.gif' = 'image/gif'
}[$extension]
if (-not $mediaType) {
    throw 'The provenance endpoint supports PNG, JPEG, WebP, and GIF image files.'
}
if (-not $PSCmdlet.ShouldProcess($file.FullName, 'Send image to OpenAI for SynthID verification')) {
    return
}
if ([string]::IsNullOrWhiteSpace($env:OPENAI_API_KEY)) {
    throw 'Set the OPENAI_API_KEY environment variable before running a live check.'
}

$stream = [IO.File]::OpenRead($file.FullName)
try {
    if ($stream.Length -eq 0 -or $stream.Length -gt 50MB) {
        throw 'The image must be nonempty and at most 50 MiB.'
    }
    $body = [Net.Http.MultipartFormDataContent]::new()
    try {
        $part = [Net.Http.StreamContent]::new($stream)
        try {
            $part.Headers.ContentType = [Net.Http.Headers.MediaTypeHeaderValue]::new($mediaType)
            $body.Add($part, 'file', ('image' + $extension))
            $token = ConvertTo-SecureString $env:OPENAI_API_KEY -AsPlainText -Force
            $reply = Invoke-RestMethod -Method Post -Uri 'https://api.openai.com/v1/content_provenance_checks' `
                -Authentication Bearer -Token $token -Body $body -MaximumRedirection 0 -TimeoutSec 60
        }
        finally { $part.Dispose() }
    }
    finally { $body.Dispose() }
}
finally { $stream.Dispose() }

$signals = @($reply.results | Where-Object { $_.type -ceq 'synthid' })
if ($reply.object -cne 'content_provenance_check' -or $signals.Count -ne 1 -or
    $signals[0].outcome -cnotin @('detected', 'not_detected')) {
    throw 'The provider response did not contain a recognized SynthID result.'
}
$signal = $signals[0]
[pscustomobject]@{
    Image = $file.FullName
    Provider = 'OpenAI'
    Signal = 'SynthID'
    Outcome = $signal.outcome
    Model = $signal.model
    GeneratedAt = $signal.generated_at
    Result = if ($signal.outcome -ceq 'detected') {
        'OpenAI reported a supported SynthID watermark'
    } else {
        'No supported SynthID watermark detected; AI use remains inconclusive'
    }
}

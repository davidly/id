# imgenai regression tests

Build `imgenai.exe` as described in the [project README](../README.md), then run:

```powershell
powershell -NoProfile -File .\tests\test-imgenai.ps1
```

The runner uses checked-in fixtures and works offline. Use `-Exe` to test another build. Place `c2patool.exe` beside that executable to run the signature, trust, and detached-manifest cases. Use `-RequireC2pa` to fail if the verifier is missing. The signed fixtures were generated and tested with c2patool 0.29.0.

The tests cover metadata declarations, editor-only matches, compressed PNG text, saved-output ComfyUI graphs, NovelAI alpha metadata, multipart JPEG XMP and C2PA, stripping, and detached credentials. The signed ingredient fixtures exercise an unchanged signature status omitted from the validation delta and an earlier ingredient hash failure that still prevents a trusted finding. The optional SynthID helper has a separate offline test with a mocked HTTP request.

```powershell
pwsh -NoProfile -File .\tests\test-synthid.ps1
```

## Fixture sources

- The synthetic files, including the baseline pattern JPEG, are generated for this project and are covered by the repository's CC0 dedication. Some format probes use placeholder image data. The signed fixtures use the c2patool development signer, and the runner reads only its public certificate chain as a test trust anchor.
- [Wikimedia Photoshop and Firefly pair](images/wikimedia/README.md) and [C2PA public camera samples](images/c2pa-org/README.md) are unchanged third-party files under CC BY-SA 4.0. Their attribution and license terms are in their own directories.

## Regenerate the synthetic fixtures

On Windows with Python 3 installed, run:

```powershell
powershell -NoProfile -File .\tests\regenerate.ps1 -C2paTool .\c2patool.exe
```

The script creates a new pattern image and uses it to rebuild the derived files and credentials. Manifest IDs and signatures can change between runs. The third-party directories are left intact.

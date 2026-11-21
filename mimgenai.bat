@echo off
setlocal
pushd "%~dp0" || exit /b 1
cl /nologo /D IMGENAI_ONLY id.cxx /I.\ /O2i /EHac /Zi /Gy /Fo:imgenai.obj /Fd:imgenai-vc.pdb /Fe:imgenai.exe /link /incremental:no /OPT:REF /PDB:imgenai.pdb
set "result=%errorlevel%"
popd
exit /b %result%

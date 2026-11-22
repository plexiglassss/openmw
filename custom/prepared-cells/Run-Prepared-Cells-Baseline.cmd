@echo off
setlocal
set "OPENMW_STAGED_ARRIVAL=0"
set "OPENMW_PREPARED_CELLS=0"
pushd "%~dp0"
openmw.exe %*
set "prepared_exit=%errorlevel%"
popd
exit /b %prepared_exit%

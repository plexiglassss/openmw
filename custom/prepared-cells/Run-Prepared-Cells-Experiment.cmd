@echo off
setlocal
set "OPENMW_STAGED_ARRIVAL=1"
set "OPENMW_PREPARED_CELLS=1"
pushd "%~dp0"
openmw.exe %*
set "prepared_exit=%errorlevel%"
popd
exit /b %prepared_exit%

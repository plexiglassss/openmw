@echo off
setlocal
set "OPENMW_ACTOR_SPATIAL_QUERIES=0"
set "OPENMW_SPATIAL_DISTANCE_QUERIES=0"
call "%~dp0Run-Combined.cmd"
exit /b %errorlevel%

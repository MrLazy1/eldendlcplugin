@echo off
setlocal
cl /nologo /LD /MT /O2 /EHsc /std:c++17 uc_dlc_unlock.cpp /link /OUT:10_dlc_unlock.dll
if errorlevel 1 exit /b 1
echo Built 10_dlc_unlock.dll

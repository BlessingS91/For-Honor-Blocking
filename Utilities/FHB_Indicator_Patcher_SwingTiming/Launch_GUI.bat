@echo off
cd /d "%~dp0"
py -3 fhb_indicator_gui.py
if errorlevel 1 pause

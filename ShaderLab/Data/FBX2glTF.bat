@echo off
cd /d %~dp0

for %%i in (*.fbx) do (
	md %%~ni
rem	.\FBX2glTF-windows-x64.exe --embed --verbose --pbr-metallic-roughness --input .\%%i --output .\%%~ni\%%~ni.gltf
	.\FBX2glTF-windows-x64.exe --binary --verbose --pbr-metallic-roughness --input .\%%i --output .\%%~ni\%%~ni.glb
)
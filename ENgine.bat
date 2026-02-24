C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat UE5_MCP_VREditor Win64 Development "C:\github\UE5_MCP_VR\UE5_MCP_VR.uproject" -waitmutex; if ($?) { Start-Process "C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList "C:\github\UE5_MCP_VR\UE5_MCP_VR.uproject" }
pause

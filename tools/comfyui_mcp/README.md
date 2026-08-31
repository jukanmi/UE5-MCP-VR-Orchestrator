# comfyui_mcp — ComfyUI MCP 브리지 서버

AI 에이전트(Antigravity / Claude)가 로컬 `http://127.0.0.1:8188`에 실행 중인 ComfyUI와 상호작용하기 위한 MCP 서버입니다.

## 기능

1. **`comfy_generate_image`**: 텍스트 프롬프트로 고화질 이미지 생성 및 로컬 파일 다운로드
2. **`comfy_queue_workflow`**: 커스텀 노드 그래프(API JSON) 실행 및 결과 확인
3. **`comfy_get_models`**: 가용 체크포인트/LoRA/VAE 모델 목록 조회
4. **`comfy_get_system_stats`**: GPU VRAM 및 시스템 상태 조회
5. **`comfy_interrupt`**: 생성 작업 즉시 중단

## 등록 방법

`mcp_config.json`의 `mcpServers`에 등록:

```json
{
  "mcpServers": {
    "comfyui": {
      "command": "c:\\github\\UE5_MCP_VR\\.venv\\Scripts\\python.exe",
      "args": [
        "c:\\github\\UE5_MCP_VR\\tools\\comfyui_mcp\\server.py"
      ],
      "env": {
        "COMFYUI_URL": "http://127.0.0.1:8188"
      }
    }
  }
}
```


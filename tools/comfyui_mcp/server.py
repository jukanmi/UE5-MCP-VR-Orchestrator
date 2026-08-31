"""ComfyUI MCP 서버 — ComfyUI REST API 프록시.

로컬/원격 ComfyUI 인스턴스(기본 http://127.0.0.1:8188)와 연동하여
AI 에이전트가 텍스트-투-이미지(T2I) 생성, 워크플로우 실행, 모델 조회,
생성 이미지 다운로드 등을 자율적으로 수행할 수 있게 해주는 MCP 서버입니다.
"""

import asyncio
import json
import os
import random
import time
from typing import Any
from urllib.parse import urlencode

import httpx
from mcp.server.fastmcp import FastMCP

BASE_URL = os.environ.get("COMFYUI_URL", "http://127.0.0.1:8188").rstrip("/")
TIMEOUT = float(os.environ.get("COMFYUI_TIMEOUT", "120"))

mcp = FastMCP("comfyui-bridge")


class ComfyUINotRunningError(RuntimeError):
    """ComfyUI 서버 미기동 또는 연결 실패."""


async def _get_client() -> httpx.AsyncClient:
    return httpx.AsyncClient(timeout=TIMEOUT)


@mcp.tool()
async def comfy_get_system_stats() -> str:
    """ComfyUI 시스템 정보 및 GPU VRAM 사용량을 조회합니다.

    Returns:
        GPU 명칭, 총 VRAM, 여유 VRAM, ComfyUI 버전 등의 JSON 문자열.
    """
    url = f"{BASE_URL}/system_stats"
    try:
        async with await _get_client() as client:
            resp = await client.get(url)
            if resp.status_code != 200:
                return f"[HTTP {resp.status_code}] {resp.text}"
            return resp.text
    except httpx.ConnectError as exc:
        raise ComfyUINotRunningError(
            f"ComfyUI 서버({BASE_URL})에 연결할 수 없습니다. ComfyUI가 실행 중인지 확인하세요."
        ) from exc


@mcp.tool()
async def comfy_get_models() -> str:
    """ComfyUI에서 사용 가능한 체크포인트 모델, LoRA, VAE 목록을 조회합니다.

    Returns:
        체크포인트(checkpoints), LoRA(loras), VAE(vaes) 목록 딕셔너리.
    """
    try:
        async with await _get_client() as client:
            ckpt_resp = await client.get(f"{BASE_URL}/object_info/CheckpointLoaderSimple")
            lora_resp = await client.get(f"{BASE_URL}/object_info/LoraLoader")
            vae_resp = await client.get(f"{BASE_URL}/object_info/VAELoader")

            checkpoints = []
            if ckpt_resp.status_code == 200:
                data = ckpt_resp.json()
                checkpoints = (
                    data.get("CheckpointLoaderSimple", {})
                    .get("input", {})
                    .get("required", {})
                    .get("ckpt_name", [[]])[0]
                )

            loras = []
            if lora_resp.status_code == 200:
                data = lora_resp.json()
                loras = data.get("LoraLoader", {}).get("input", {}).get("required", {}).get("lora_name", [[]])[0]

            vaes = []
            if vae_resp.status_code == 200:
                data = vae_resp.json()
                vaes = data.get("VAELoader", {}).get("input", {}).get("required", {}).get("vae_name", [[]])[0]

            result = {
                "checkpoints": checkpoints,
                "loras": loras,
                "vaes": vaes,
            }
            return json.dumps(result, ensure_ascii=False, indent=2)
    except httpx.ConnectError as exc:
        raise ComfyUINotRunningError(f"ComfyUI 서버({BASE_URL}) 연결 실패") from exc


@mcp.tool()
async def comfy_interrupt() -> str:
    """현재 진행 중인 이미지 생성 작업을 즉시 중단합니다."""
    try:
        async with await _get_client() as client:
            resp = await client.post(f"{BASE_URL}/interrupt")
            return (
                "생성 작업이 중단되었습니다." if resp.status_code == 200 else f"[HTTP {resp.status_code}] {resp.text}"
            )
    except httpx.ConnectError as exc:
        raise ComfyUINotRunningError(f"ComfyUI 서버({BASE_URL}) 연결 실패") from exc


@mcp.tool()
async def comfy_queue_workflow(workflow_json: str, wait_for_completion: bool = True) -> str:
    """임의의 ComfyUI 워크플로우(Prompt API JSON)를 큐에 등록하고 결과를 확인합니다.

    Args:
        workflow_json: ComfyUI의 'Save (API Format)'으로 내보낸 노드 그래프 JSON 문자열.
        wait_for_completion: True일 경우 생성이 끝날 때까지 대기한 후 생성된 파일 정보 반환 (기본 True).

    Returns:
        생성 결과 정보 (prompt_id 및 출력 이미지 목록).
    """
    try:
        prompt_data = json.loads(workflow_json)
    except json.JSONDecodeError as exc:
        return f"JSON 파싱 실패: {exc}"

    client_id = f"mcp_agent_{int(time.time())}"
    payload = {"prompt": prompt_data, "client_id": client_id}

    try:
        async with await _get_client() as client:
            resp = await client.post(f"{BASE_URL}/prompt", json=payload)
            if resp.status_code != 200:
                return f"[큐 등록 실패 HTTP {resp.status_code}] {resp.text}"

            res_json = resp.json()
            prompt_id = res_json.get("prompt_id")
            if not prompt_id:
                return f"prompt_id 없음: {res_json}"

            if not wait_for_completion:
                return json.dumps({"status": "queued", "prompt_id": prompt_id}, ensure_ascii=False)

            # 완료 대기 (폴링)
            start_time = time.time()
            while time.time() - start_time < TIMEOUT:
                await asyncio.sleep(1.0)
                hist_resp = await client.get(f"{BASE_URL}/history/{prompt_id}")
                if hist_resp.status_code == 200:
                    hist_data = hist_resp.json()
                    if prompt_id in hist_data:
                        task_info = hist_data[prompt_id]
                        status = task_info.get("status", {})

                        # 실패한 작업도 outputs 키를 달고 오므로 상태를 먼저 본다.
                        # 이걸 빼면 노드 오류가 "완료됐는데 이미지가 없다"로 둔갑해 원인을 못 찾는다.
                        if status.get("status_str") == "error":
                            return json.dumps(
                                {
                                    "status": "error",
                                    "prompt_id": prompt_id,
                                    "messages": status.get("messages", []),
                                },
                                ensure_ascii=False,
                                indent=2,
                            )

                        if status.get("completed", False) or "outputs" in task_info:
                            outputs = task_info.get("outputs", {})
                            images = []
                            for node_id, node_out in outputs.items():
                                if "images" in node_out:
                                    for img in node_out["images"]:
                                        img_url = f"{BASE_URL}/view?{urlencode(img)}"
                                        images.append(
                                            {
                                                "filename": img.get("filename"),
                                                "subfolder": img.get("subfolder"),
                                                "type": img.get("type"),
                                                "url": img_url,
                                            }
                                        )
                            return json.dumps(
                                {"status": "completed", "prompt_id": prompt_id, "images": images, "outputs": outputs},
                                ensure_ascii=False,
                                indent=2,
                            )

            return json.dumps(
                {"status": "timeout", "prompt_id": prompt_id, "message": "작업 시간 초과"}, ensure_ascii=False
            )
    except httpx.ConnectError as exc:
        raise ComfyUINotRunningError(f"ComfyUI 서버({BASE_URL}) 연결 실패") from exc


@mcp.tool()
async def comfy_generate_image(
    prompt: str,
    negative_prompt: str = "bad quality, blurry, deformed, disfigured, lowres, watermark",
    checkpoint: str = "",
    width: int = 1024,
    height: int = 1024,
    steps: int = 25,
    cfg: float = 7.0,
    sampler_name: str = "euler_ancestral",
    scheduler: str = "normal",
    seed: int = -1,
    save_local_path: str = "",
) -> str:
    """텍스트 프롬프트로부터 이미지를 생성하고 로컬 파일로 저장합니다.

    Args:
        prompt: 긍정 프롬프트 (생성할 대상 설명).
        negative_prompt: 부정 프롬프트 (제외할 요소).
        checkpoint: 사용할 체크포인트 모델 파일명 (비워두면 첫 번째 가용 SDXL 모델 자동 선택).
        width: 이미지 가로 해상도 (기본 1024).
        height: 이미지 세로 해상도 (기본 1024).
        steps: 샘플링 스텝 수 (기본 25).
        cfg: 프롬프트 가중치 (기본 7.0).
        sampler_name: 샘플러 (기본 euler_ancestral).
        scheduler: 스케줄러 (기본 normal).
        seed: 랜덤 시드 (-1이면 임의 생성).
        save_local_path: 생성된 이미지를 다운로드하여 저장할 로컬 파일 경로 (비워두면 artifacts 또는 다운로드 폴더에 자동 저장).

    Returns:
        생성 결과 정보 및 로컬 파일 경로 JSON.
    """
    if seed == -1:
        seed = random.randint(1, 1000000000000)

    # 모델 자동 선택
    try:
        async with await _get_client() as client:
            if not checkpoint:
                models_data = json.loads(await comfy_get_models())
                ckpts = models_data.get("checkpoints", [])
                if not ckpts:
                    return "사용 가능한 체크포인트 모델이 ComfyUI에 없습니다."
                # SDXL 우선 선택
                sdxl_candidates = [
                    c for c in ckpts if "xl" in c.lower() or "pony" in c.lower() or "illustrious" in c.lower()
                ]
                checkpoint = sdxl_candidates[0] if sdxl_candidates else ckpts[0]

            # 표준 SDXL 워크플로우 그래프 생성
            workflow = {
                "3": {
                    "inputs": {
                        "seed": seed,
                        "steps": steps,
                        "cfg": cfg,
                        "sampler_name": sampler_name,
                        "scheduler": scheduler,
                        "denoise": 1.0,
                        "model": ["4", 0],
                        "positive": ["6", 0],
                        "negative": ["7", 0],
                        "latent_image": ["5", 0],
                    },
                    "class_type": "KSampler",
                },
                "4": {"inputs": {"ckpt_name": checkpoint}, "class_type": "CheckpointLoaderSimple"},
                "5": {"inputs": {"width": width, "height": height, "batch_size": 1}, "class_type": "EmptyLatentImage"},
                "6": {"inputs": {"text": prompt, "clip": ["4", 1]}, "class_type": "CLIPTextEncode"},
                "7": {"inputs": {"text": negative_prompt, "clip": ["4", 1]}, "class_type": "CLIPTextEncode"},
                "8": {"inputs": {"samples": ["3", 0], "vae": ["4", 2]}, "class_type": "VAEDecode"},
                "9": {
                    "inputs": {"filename_prefix": "Antigravity_ComfyUI", "images": ["8", 0]},
                    "class_type": "SaveImage",
                },
            }

            # 큐 등록 및 대기
            queue_res_str = await comfy_queue_workflow(json.dumps(workflow), wait_for_completion=True)
            res_data = json.loads(queue_res_str)

            if res_data.get("status") != "completed":
                return f"이미지 생성 실패: {queue_res_str}"

            images = res_data.get("images", [])
            if not images:
                return f"생성된 이미지를 찾을 수 없습니다: {queue_res_str}"

            first_img = images[0]
            img_url = first_img["url"]

            # 로컬 저장 경로 확정
            if not save_local_path:
                save_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "outputs")
                os.makedirs(save_dir, exist_ok=True)
                save_local_path = os.path.join(save_dir, first_img["filename"])
            else:
                os.makedirs(os.path.dirname(os.path.abspath(save_local_path)), exist_ok=True)

            # 이미지 다운로드 및 로컬 저장
            img_resp = await client.get(img_url)
            if img_resp.status_code == 200:
                with open(save_local_path, "wb") as f:
                    f.write(img_resp.content)
                res_data["local_file_path"] = os.path.abspath(save_local_path)

            res_data["checkpoint_used"] = checkpoint
            res_data["seed_used"] = seed
            return json.dumps(res_data, ensure_ascii=False, indent=2)

    except httpx.ConnectError as exc:
        raise ComfyUINotRunningError(f"ComfyUI 서버({BASE_URL}) 연결 실패") from exc


if __name__ == "__main__":
    mcp.run()

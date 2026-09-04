"""기존 정제 완료 GLB(형상 전용)에 Hunyuan3D-Paint 로 텍스처를 입힌다.

파이프라인:
  아이콘 PNG -> 배경제거 -> 회색(204) 캔버스 512x512  (delight 입력)
  Hy3DLoadMesh -> Hy3DMeshUVWrap -> Hy3DRenderMultiView (normal/position map + renderer)
  Hy3DDelightImage(ref) + multiview 맵 -> Hy3DSampleMultiView -> Hy3DBakeFromMultiview
  -> Hy3DMeshVerticeInpaintTexture -> CV2InpaintTexture -> Hy3DApplyTexture -> Hy3DExportMesh

출력은 원본을 덮지 않고 Art/Meshes/Items_Textured/ 에 저장한다.
텍스처 아틀라스 PNG 도 QA 용으로 함께 저장한다.
"""

import asyncio
import io
import json
import os
import sys
import time
from urllib.parse import urlencode

import httpx
from PIL import Image
from rembg import remove

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

COMFYUI_URL = os.environ.get("COMFYUI_URL", "http://127.0.0.1:8188").rstrip("/")
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
ICON_DIR = os.path.join(REPO_ROOT, "Art/Icons/Items")
PAINT_ICON_DIR = os.path.join(REPO_ROOT, "Art/Icons/Items_PaintRef")
MESH_DIR = os.path.join(REPO_ROOT, "Art/Meshes/Items")
OUT_DIR = os.path.join(REPO_ROOT, "Art/Meshes/Items_Textured")
TEX_DIR = os.path.join(REPO_ROOT, "Art/Meshes/Textures")

os.makedirs(PAINT_ICON_DIR, exist_ok=True)
os.makedirs(OUT_DIR, exist_ok=True)
os.makedirs(TEX_DIR, exist_ok=True)


# ComfyUI 서버가 WSL 안에서 돌기 때문에 메시 경로는 리눅스 마운트 경로로 넘긴다.
def to_server_path(win_path: str) -> str:
    p = os.path.abspath(win_path).replace("\\", "/")
    if len(p) > 1 and p[1] == ":":
        return f"/mnt/{p[0].lower()}{p[2:]}"
    return p


PAINT_MODEL = "hunyuan3d-paint-v2-0-turbo"
DELIGHT_MODEL = "hunyuan3d-delight-v2-0"
RENDER_SIZE = 1024
TEXTURE_SIZE = 1024
# VR 손에 드는 인벤토리 소품 기준 목표 삼각형 수. 72종 합쳐 약 216K tri 로 Quest 급도 감당된다.
TARGET_FACES = 3000
VIEW_SIZE = 512
SAMPLE_STEPS = 15  # turbo 모델 기준
DELIGHT_STEPS = 25
TIMEOUT_SECONDS = 1800

# Hy3DExportMesh 는 STRING 만 돌려줘 history outputs 에 안 잡힌다. 서버 출력 폴더에서 직접 회수한다.
COMFY_OUTPUT = os.path.expanduser(os.environ.get("COMFY_OUTPUT", "~/ComfyUI/output"))

# delight 입력 배경. 완전 흰색은 과노출, 완전 검정은 실패 -> 중간 회색.
REF_BG = (204, 204, 204)
REF_SIZE = 512


def preprocess_paint_ref(item_id: str) -> str:
    """아이콘 배경 제거 후 회색 캔버스 정중앙에 맞춰 delight 참조 이미지를 만든다."""
    raw_icon = os.path.join(ICON_DIR, f"{item_id}.png")
    ref_path = os.path.join(PAINT_ICON_DIR, f"{item_id}_ref.png")
    if not os.path.exists(raw_icon):
        return ""

    img = Image.open(raw_icon)
    nobg = remove(img)
    if nobg.mode != "RGBA":
        nobg = nobg.convert("RGBA")

    bbox = nobg.getbbox()
    if bbox:
        nobg = nobg.crop(bbox)

    # 가장자리 여백 6% 를 남기고 정사각 캔버스에 맞춤
    target = int(REF_SIZE * 0.88)
    scale = min(target / nobg.width, target / nobg.height)
    new_size = (max(1, int(nobg.width * scale)), max(1, int(nobg.height * scale)))
    nobg = nobg.resize(new_size, Image.LANCZOS)

    canvas = Image.new("RGB", (REF_SIZE, REF_SIZE), REF_BG)
    offset = ((REF_SIZE - new_size[0]) // 2, (REF_SIZE - new_size[1]) // 2)
    canvas.paste(nobg, offset, mask=nobg.split()[3])
    canvas.save(ref_path)
    return ref_path


async def upload_image(client: httpx.AsyncClient, file_path: str, upload_name: str) -> bool:
    with open(file_path, "rb") as f:
        files = {"image": (upload_name, f, "image/png")}
        data = {"overwrite": "true"}
        resp = await client.post(f"{COMFYUI_URL}/upload/image", files=files, data=data)
        return resp.status_code == 200


def build_workflow(item_id: str, ref_image_name: str, seed: int) -> dict:
    mesh_path = to_server_path(os.path.join(MESH_DIR, f"{item_id}.glb"))
    return {
        "1": {"inputs": {"image": ref_image_name}, "class_type": "LoadImage"},
        "2": {"inputs": {"model": DELIGHT_MODEL}, "class_type": "DownloadAndLoadHy3DDelightModel"},
        "3": {
            "inputs": {
                "delight_pipe": ["2", 0],
                "image": ["1", 0],
                "steps": DELIGHT_STEPS,
                "width": REF_SIZE,
                "height": REF_SIZE,
                "cfg_image": 1.0,
                "seed": seed,
            },
            "class_type": "Hy3DDelightImage",
        },
        "4": {"inputs": {"glb_path": mesh_path}, "class_type": "Hy3DLoadMesh"},
        # 감폴리는 반드시 UV 언랩 전에. 언랩·베이크 후에 줄이면 UV 가 깨져 텍스처를 다시 구워야 한다.
        "4b": {
            "inputs": {
                "trimesh": ["4", 0],
                "remove_floaters": True,
                "remove_degenerate_faces": True,
                "reduce_faces": True,
                "max_facenum": TARGET_FACES,
                "smooth_normals": True,
            },
            "class_type": "Hy3DPostprocessMesh",
        },
        # Hy3DPostprocessMesh 의 감폴리는 preservetopology=True·qualitythr=1.0 이 하드코딩돼 있어
        # 구멍 많은 복셀 메시에서 목표치의 7배 근처에서 멈춘다. 경계 보존을 끈 2단 감폴리로 목표를 강제한다.
        "4c": {
            "inputs": {
                "trimesh": ["4b", 0],
                "target_count": TARGET_FACES,
                "aggressiveness": 7,
                "max_iterations": 100,
                "update_rate": 5,
                "preserve_border": False,
                "lossless": False,
                "threshold_lossless": 0.001,
            },
            "class_type": "Hy3DFastSimplifyMesh",
        },
        "5": {"inputs": {"trimesh": ["4c", 0]}, "class_type": "Hy3DMeshUVWrap"},
        "6": {
            "inputs": {
                # 얇은 형상(잔·창·안경)은 6뷰로는 그레이징 앵글만 잡혀 텍셀 절반 이상이 미착색으로 남는다.
                # 대각 45도 4뷰를 추가해 커버리지를 늘린다.
                "camera_azimuths": "0, 90, 180, 270, 45, 135, 225, 315, 0, 180",
                # Hy3DSampleMultiView 가 elevation 을 {-90,-45,-20,0,20,45,90} 로만 받는다. 그 외 값은 KeyError.
                "camera_elevations": "0, 0, 0, 0, 45, 45, -45, -45, 90, -90",
                "view_weights": "1, 0.3, 0.5, 0.3, 0.2, 0.2, 0.2, 0.2, 0.1, 0.1",
                "camera_distance": 1.45,
                "ortho_scale": 1.2,
            },
            "class_type": "Hy3DCameraConfig",
        },
        "7": {
            "inputs": {
                "trimesh": ["5", 0],
                "render_size": RENDER_SIZE,
                "texture_size": TEXTURE_SIZE,
                "camera_config": ["6", 0],
                "normal_space": "world",
            },
            "class_type": "Hy3DRenderMultiView",
        },
        "8": {"inputs": {"model": PAINT_MODEL}, "class_type": "DownloadAndLoadHy3DPaintModel"},
        "9": {
            "inputs": {
                "pipeline": ["8", 0],
                "ref_image": ["3", 0],
                "normal_maps": ["7", 0],
                "position_maps": ["7", 1],
                "view_size": VIEW_SIZE,
                "steps": SAMPLE_STEPS,
                "seed": seed,
                "camera_config": ["6", 0],
            },
            "class_type": "Hy3DSampleMultiView",
        },
        "10": {
            "inputs": {"images": ["9", 0], "renderer": ["7", 2], "camera_config": ["6", 0]},
            "class_type": "Hy3DBakeFromMultiview",
        },
        "11": {
            "inputs": {"texture": ["10", 0], "mask": ["10", 1], "renderer": ["10", 2]},
            "class_type": "Hy3DMeshVerticeInpaintTexture",
        },
        "12": {
            "inputs": {"texture": ["11", 0], "mask": ["11", 1], "inpaint_radius": 3, "inpaint_method": "ns"},
            "class_type": "CV2InpaintTexture",
        },
        "13": {"inputs": {"texture": ["12", 0], "renderer": ["11", 2]}, "class_type": "Hy3DApplyTexture"},
        "14": {
            "inputs": {
                "trimesh": ["13", 0],
                "filename_prefix": f"3D/Tex_{item_id}",
                "file_format": "glb",
                "save_file": True,
            },
            "class_type": "Hy3DExportMesh",
        },
        "15": {"inputs": {"images": ["12", 0], "filename_prefix": f"textures/{item_id}"}, "class_type": "SaveImage"},
        # 페인트 모델이 그린 6방향 뷰. 텍스처 품질 육안 판정용.
        "16": {"inputs": {"images": ["9", 0], "filename_prefix": f"multiview/{item_id}"}, "class_type": "SaveImage"},
    }


async def wait_for_prompt(client: httpx.AsyncClient, prompt_id: str, deadline: float) -> dict:
    while time.time() < deadline:
        await asyncio.sleep(3.0)
        resp = await client.get(f"{COMFYUI_URL}/history/{prompt_id}")
        if resp.status_code != 200:
            continue
        data = resp.json()
        if prompt_id not in data:
            continue
        entry = data[prompt_id]
        status = entry.get("status", {})
        if status.get("completed") or status.get("status_str") == "success":
            return entry
        if status.get("status_str") == "error":
            return entry
    return {}


async def process_item(client: httpx.AsyncClient, item_id: str, index: int, total: int, seed: int) -> bool:
    mesh_file = os.path.join(MESH_DIR, f"{item_id}.glb")
    if not os.path.exists(mesh_file):
        print(f"[{index}/{total}] ❌ {item_id}: GLB 없음")
        return False

    ref_path = preprocess_paint_ref(item_id)
    if not ref_path:
        print(f"[{index}/{total}] ❌ {item_id}: 아이콘 없음")
        return False

    upload_name = f"{item_id}_paintref.png"
    if not await upload_image(client, ref_path, upload_name):
        print(f"[{index}/{total}] ❌ {item_id}: 참조 이미지 업로드 실패")
        return False

    workflow = build_workflow(item_id, upload_name, seed)
    start = time.time()
    print(f"[{index}/{total}] 🎨 텍스처 생성: {item_id} ...")

    resp = await client.post(f"{COMFYUI_URL}/prompt", json={"prompt": workflow, "client_id": f"tex_{item_id}"})
    if resp.status_code != 200:
        print(f"[{index}/{total}] ❌ 큐 등록 실패: {resp.text[:400]}")
        return False

    prompt_id = resp.json().get("prompt_id")
    entry = await wait_for_prompt(client, prompt_id, start + TIMEOUT_SECONDS)
    if not entry:
        print(f"[{index}/{total}] ⚠️ {item_id}: 타임아웃")
        return False

    status = entry.get("status", {})
    if status.get("status_str") == "error":
        msgs = [m for m in status.get("messages", []) if m and m[0] == "execution_error"]
        print(f"[{index}/{total}] ❌ {item_id} 실행 에러: {json.dumps(msgs, ensure_ascii=False)[:700]}")
        return False

    outputs = entry.get("outputs", {})

    # 텍스처 아틀라스 회수
    tex_out = outputs.get("15", {})
    if tex_out.get("images"):
        img_resp = await client.get(f"{COMFYUI_URL}/view?{urlencode(tex_out['images'][0])}")
        if img_resp.status_code == 200:
            with open(os.path.join(TEX_DIR, f"{item_id}_texture.png"), "wb") as f:
                f.write(img_resp.content)

    # 페인팅된 6방향 뷰를 가로 시트 1장으로 합쳐 QA 용으로 저장
    mv_out = outputs.get("16", {})
    if mv_out.get("images"):
        views = []
        for info in mv_out["images"]:
            r = await client.get(f"{COMFYUI_URL}/view?{urlencode(info)}")
            if r.status_code == 200:
                views.append(Image.open(io.BytesIO(r.content)).convert("RGB"))
        if views:
            w, h = views[0].size
            sheet = Image.new("RGB", (w * len(views), h))
            for i, im in enumerate(views):
                sheet.paste(im, (i * w, 0))
            sheet.save(os.path.join(TEX_DIR, f"{item_id}_multiview.png"))

    # 텍스처 적용된 GLB 회수 (Hy3DExportMesh 는 UI 출력이 없어 서버 출력 폴더에서 최신 파일을 집는다)
    glb_written = False
    export_dir = os.path.join(COMFY_OUTPUT, "3D")
    if os.path.isdir(export_dir):
        candidates = [
            os.path.join(export_dir, f)
            for f in os.listdir(export_dir)
            if f.startswith(f"Tex_{item_id}_") and f.endswith(".glb")
        ]
        if candidates:
            src = max(candidates, key=os.path.getmtime)
            with open(src, "rb") as sf, open(os.path.join(OUT_DIR, f"{item_id}.glb"), "wb") as df:
                df.write(sf.read())
            glb_written = True

    elapsed = time.time() - start
    if glb_written:
        size_mb = os.path.getsize(os.path.join(OUT_DIR, f"{item_id}.glb")) / (1024 * 1024)
        print(f"[{index}/{total}] ✅ {item_id}: 텍스처 완료 ({elapsed:.1f}s, {size_mb:.1f}MB)")
        return True

    print(f"[{index}/{total}] ⚠️ {item_id}: GLB 회수 실패. outputs keys={list(outputs.keys())}")
    return False


async def main(targets):
    total = len(targets)
    print(f"=== Hunyuan3D-Paint 텍스처 생성 ({total}종) ===")
    ok = 0
    async with httpx.AsyncClient(timeout=TIMEOUT_SECONDS + 60) as client:
        for i, item_id in enumerate(targets, 1):
            try:
                if await process_item(client, item_id, i, total, seed=42):
                    ok += 1
            except Exception as e:
                print(f"[{i}/{total}] ❌ {item_id}: 예외 {e}")
    print(f"\n=== 완료: {ok}/{total} 성공 ===")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] != "all":
        items = [t.strip() for t in sys.argv[1].split(",")]
    else:
        items = sorted(os.path.splitext(f)[0] for f in os.listdir(MESH_DIR) if f.endswith(".glb"))
    asyncio.run(main(items))

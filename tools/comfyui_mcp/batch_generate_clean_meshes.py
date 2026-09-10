"""42종 미커밋 아이템 전용 클린 3D 메시 재생성 및 3방향 정밀 검증기 (v2).

개선 사항:
1. [배경 누끼 전처리]: rembg(u2net)로 바닥 그림자/배경 100% 제거 -> 순수 흰색 캔버스에 오브젝트 중앙 격리 (바닥 판/받침대 생성 원천 차단)
2. [지오메트리 튜닝]: threshold 0.65 적용 (과도한 두께 및 부유 파편 억제)
3. [3방향 렌더링]: 생성 즉시 정면(Front)/측면(Side)/입체(Iso) 3-View 스냅샷 렌더링
4. [타겟]: 사용자가 이미 커밋한 30종 제외, 미커밋 42종만 정밀 재생성
"""

import asyncio
import csv
import json
import os
import random
import sys
import time
from urllib.parse import urlencode

import httpx
from PIL import Image
from rembg import remove
import numpy as np

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import trimesh

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

COMFYUI_URL = os.environ.get("COMFYUI_URL", "http://127.0.0.1:8188").rstrip("/")
ICON_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../Art/Icons/Items"))
CLEAN_ICON_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../Art/Icons/Items_Clean"))
OUTPUT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../Art/Meshes/Items"))
SNAPSHOT_3VIEW_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../Art/Meshes/Snapshots_3View"))

os.makedirs(CLEAN_ICON_DIR, exist_ok=True)
os.makedirs(OUTPUT_DIR, exist_ok=True)
os.makedirs(SNAPSHOT_3VIEW_DIR, exist_ok=True)

# 3D 생성 파라미터 (클린 모드)
CHECKPOINT = "hunyuan3d-dit-v2-mv-turbo.safetensors"
LATENT_RESOLUTION = 3072
OCTREE_RESOLUTION = 256
NUM_CHUNKS = 8000
STEPS = 30
CFG = 1.0
SHIFT = 1.0
SAMPLER = "euler"
SCHEDULER = "normal"
MESH_ALGORITHM = "surface net"
MESH_THRESHOLD = 0.65  # 바닥 및 두께 완화를 위해 0.6 -> 0.65 상향
TIMEOUT_SECONDS = 300

# 미커밋 42종 타겟 목록
TARGET_UNCOMMITTED_42 = [
    "AlchemistryVial", "AncientScroll", "Arrow_Bundle", "Bread", "BribeCoin",
    "BrokenCompass", "CoinPouch", "CrystalBall", "DivingHelmet", "FeatherCap",
    "ForgeHammer", "Glasses", "GuardShield", "GuardSpear", "HealthPotion",
    "HerbExtract", "HerbTea", "HolyWater", "HonorFlower", "HuntingBow",
    "IronOre", "Leather", "Lockpick", "MagicGem", "MagnifyingGlass",
    "Microphone", "OathScroll", "PassDoc", "PoisonDagger", "ProphecyScroll",
    "QuillPen", "RumBottle", "Shield_Knight", "SmokeVial", "SpellScroll",
    "StarMap", "Torch", "TreasureKey", "WaterBucket", "Whetstone",
    "Whistle", "WineCup"
]


def preprocess_clean_icon(item_id: str) -> str:
    """원본 아이콘에서 배경을 완전히 제거하고 흰색 캔버스에 정렬하여 저장한다."""
    raw_icon = os.path.join(ICON_DIR, f"{item_id}.png")
    clean_icon = os.path.join(CLEAN_ICON_DIR, f"{item_id}_clean.png")

    if not os.path.exists(raw_icon):
        return ""

    img = Image.open(raw_icon)
    nobg = remove(img)
    
    white_bg = Image.new("RGB", nobg.size, (255, 255, 255))
    if nobg.mode == "RGBA":
        white_bg.paste(nobg, mask=nobg.split()[3])
    else:
        white_bg.paste(nobg)
        
    white_bg.save(clean_icon)
    return clean_icon


async def upload_image(client: httpx.AsyncClient, file_path: str, upload_name: str) -> bool:
    """ComfyUI에 이미지를 업로드한다."""
    try:
        with open(file_path, "rb") as f:
            files = {"image": (upload_name, f, "image/png")}
            data = {"overwrite": "true"}
            resp = await client.post(f"{COMFYUI_URL}/upload/image", files=files, data=data)
            return resp.status_code == 200
    except Exception as e:
        print(f"  [에러] 업로드 실패: {e}")
        return False


def build_workflow(image_filename: str, item_id: str, seed: int) -> dict:
    """클린 Hunyuan3D Shape 워크플로우 구성."""
    return {
        "10": {"inputs": {"image": image_filename}, "class_type": "LoadImage"},
        "11": {"inputs": {"ckpt_name": CHECKPOINT}, "class_type": "ImageOnlyCheckpointLoader"},
        "13": {"inputs": {"image": ["10", 0], "clip_vision": ["11", 1], "crop": "center"}, "class_type": "CLIPVisionEncode"},
        "12": {"inputs": {"clip_vision_output": ["13", 0]}, "class_type": "Hunyuan3Dv2Conditioning"},
        "14": {"inputs": {"resolution": LATENT_RESOLUTION, "batch_size": 1}, "class_type": "EmptyLatentHunyuan3Dv2"},
        "15": {"inputs": {"model": ["11", 0], "shift": SHIFT}, "class_type": "ModelSamplingAuraFlow"},
        "16": {
            "inputs": {
                "seed": seed, "steps": STEPS, "cfg": CFG, "sampler_name": SAMPLER,
                "scheduler": SCHEDULER, "denoise": 1.0, "model": ["15", 0],
                "positive": ["12", 0], "negative": ["12", 1], "latent_image": ["14", 0]
            },
            "class_type": "KSampler"
        },
        "17": {
            "inputs": {
                "samples": ["16", 0], "vae": ["11", 2], "num_chunks": NUM_CHUNKS,
                "octree_resolution": OCTREE_RESOLUTION
            },
            "class_type": "VAEDecodeHunyuan3D"
        },
        "18": {"inputs": {"voxel": ["17", 0], "algorithm": MESH_ALGORITHM, "threshold": MESH_THRESHOLD}, "class_type": "VoxelToMesh"},
        "19": {"inputs": {"mesh": ["18", 0], "filename_prefix": f"3d/Clean_{item_id}"}, "class_type": "SaveGLB"}
    }


def render_3view(mesh_path: str, item_id: str, out_path: str):
    """정면, 측면, 등각 3방향 렌더링 스냅샷을 1장의 가로 이미지로 저장한다."""
    try:
        scene = trimesh.load(mesh_path)
        if isinstance(scene, trimesh.Scene):
            meshes = [g for g in scene.geometry.values() if isinstance(g, trimesh.Trimesh)]
            mesh = trimesh.util.concatenate(meshes) if meshes else None
        else:
            mesh = scene

        if mesh is None or len(mesh.vertices) == 0:
            return

        fig = plt.figure(figsize=(15, 5), dpi=90, facecolor="#14141e")
        views = [
            {"title": "Front View", "elev": 0, "azim": 0, "pos": 131},
            {"title": "Side View", "elev": 0, "azim": 90, "pos": 132},
            {"title": "Isometric View", "elev": 28, "azim": 45, "pos": 133},
        ]

        verts = mesh.vertices
        faces = mesh.faces
        if len(faces) > 12000:
            step = len(faces) // 12000
            render_faces = faces[::step]
        else:
            render_faces = faces

        triangles = verts[render_faces]
        v0, v1, v2 = triangles[:, 0], triangles[:, 1], triangles[:, 2]
        normals = np.cross(v1 - v0, v2 - v0)
        normals = normals / (np.linalg.norm(normals, axis=1, keepdims=True) + 1e-8)

        light_dir = np.array([0.5, 0.5, 1.0])
        light_dir /= np.linalg.norm(light_dir)
        intensity = np.clip(np.dot(normals, light_dir), 0.2, 1.0)

        colors = np.zeros((len(render_faces), 4))
        colors[:, 0] = 0.55 * intensity
        colors[:, 1] = 0.72 * intensity
        colors[:, 2] = 0.95 * intensity
        colors[:, 3] = 1.0

        min_b = verts.min(axis=0)
        max_b = verts.max(axis=0)
        center = (min_b + max_b) / 2.0
        max_range = (max_b - min_b).max() / 2.0

        for v_info in views:
            ax = fig.add_subplot(v_info["pos"], projection="3d", facecolor="#14141e")
            ax.set_axis_off()
            collection = Poly3DCollection(triangles, facecolors=colors, edgecolors="none", shade=False)
            ax.add_collection3d(collection)
            ax.set_xlim(center[0] - max_range, center[0] + max_range)
            ax.set_ylim(center[1] - max_range, center[1] + max_range)
            ax.set_zlim(center[2] - max_range, center[2] + max_range)
            ax.view_init(elev=v_info["elev"], azim=v_info["azim"])
            ax.set_title(v_info["title"], color="#b0b0c0", fontsize=10, pad=-2)

        fig.suptitle(f"{item_id} (Clean v2)  |  Vertices: {len(verts):,}  |  Faces: {len(faces):,}", color="#ffffff", fontsize=13, y=0.98)
        plt.tight_layout()
        plt.savefig(out_path, dpi=90, facecolor=fig.get_facecolor(), edgecolor="none")
        plt.close(fig)
    except Exception as e:
        print(f"  [렌더 에러] {item_id}: {e}")


async def process_single_item(client: httpx.AsyncClient, item_id: str, index: int, total: int) -> bool:
    """단일 아이템 배경 누끼 -> 3D 생성 -> GLB 다운로드 -> 3방향 렌더링."""
    out_glb = os.path.join(OUTPUT_DIR, f"{item_id}.glb")
    out_3view = os.path.join(SNAPSHOT_3VIEW_DIR, f"{item_id}_3view.png")

    clean_icon_path = preprocess_clean_icon(item_id)
    if not clean_icon_path:
        print(f"[{index}/{total}] ❌ {item_id}: 원본 아이콘 누락")
        return False

    upload_name = f"{item_id}_clean.png"
    uploaded = await upload_image(client, clean_icon_path, upload_name)
    if not uploaded:
        return False

    seed = random.randint(1, 1000000000)
    workflow = build_workflow(upload_name, item_id, seed)

    print(f"[{index}/{total}] 🎨 클린 3D 생성 시작: {item_id} (누끼 적용, threshold=0.65)...")
    start_time = time.time()

    resp = await client.post(f"{COMFYUI_URL}/prompt", json={"prompt": workflow, "client_id": f"clean_{item_id}"})
    if resp.status_code != 200:
        print(f"[{index}/{total}] ❌ 큐 등록 실패: {resp.text}")
        return False

    prompt_id = resp.json().get("prompt_id")

    while time.time() - start_time < TIMEOUT_SECONDS:
        await asyncio.sleep(2.0)
        hist_resp = await client.get(f"{COMFYUI_URL}/history/{prompt_id}")
        if hist_resp.status_code == 200:
            hist_data = hist_resp.json()
            if prompt_id in hist_data:
                task = hist_data[prompt_id]
                outputs = task.get("outputs", {})
                if "19" in outputs and "3d" in outputs["19"]:
                    glb_info = outputs["19"]["3d"][0]
                    glb_url = f"{COMFYUI_URL}/view?{urlencode(glb_info)}"
                    glb_data = await client.get(glb_url)
                    if glb_data.status_code == 200:
                        with open(out_glb, "wb") as f:
                            f.write(glb_data.content)
                        elapsed = time.time() - start_time
                        size_mb = len(glb_data.content) / (1024 * 1024)
                        print(f"[{index}/{total}] ✅ GLB 저장 완료: {item_id} ({elapsed:.1f}초, {size_mb:.1f}MB)")

                        render_3view(out_glb, item_id, out_3view)
                        print(f"[{index}/{total}] 📸 3방향 스냅샷 저장: {item_id}_3view.png")
                        return True
                    else:
                        return False

    print(f"[{index}/{total}] ⚠️ 타임아웃: {item_id}")
    return False


async def main(target: str = "uncommitted"):
    if target == "sample":
        items = ["GuardSpear", "Bread", "HealthPotion", "Torch", "Shield_Knight"]
        print(f"=== [클린 3D v2 표본 검증 모드 ({len(items)}종)] ===")
    elif target == "all" or target == "uncommitted":
        items = TARGET_UNCOMMITTED_42
        print(f"=== [미커밋 42종 클린 3D 전량 재생성 모드 ({len(items)}종)] ===")
    else:
        items = [t.strip() for t in target.split(",")]
        print(f"=== [지정 아이템 클린 3D 모드 ({len(items)}종)] ===")

    total = len(items)
    print(f"타겟 아이템 목록: {', '.join(items)}")
    print(f"ComfyUI URL: {COMFYUI_URL}\n")

    success_count = 0
    async with httpx.AsyncClient(timeout=TIMEOUT_SECONDS + 30) as client:
        for i, item_id in enumerate(items, 1):
            success = await process_single_item(client, item_id, i, total)
            if success:
                success_count += 1

    print(f"\n=== 작업 완료: {success_count}/{total} 성공 ===")


if __name__ == "__main__":
    mode_arg = sys.argv[1] if len(sys.argv) > 1 else "uncommitted"
    asyncio.run(main(mode_arg))


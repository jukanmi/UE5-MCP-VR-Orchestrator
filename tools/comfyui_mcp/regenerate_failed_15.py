"""불합격 15종 아이템 전용 고품질 3D 재생성 및 정제기.

대상 15종:
1. AncientScroll  (솔리드 닫힌 양피지 롤)
2. BribeCoin      (단일 주화, 선명한 엣지)
3. BrokenCompass  (단일 나침반 케이스)
4. DivingHelmet   (솔리드 황동 잠수 헬멧)
5. FeatherCap     (깃털 모자)
6. ForgeHammer    (대장간 망치)
7. Glasses        (접힌 형태의 슬림 안경)
8. HuntingBow     (단일 목재 활)
9. Leather        (말린 가죽 롤)
10. PassDoc       (단일 양피지 서류)
11. SpellScroll   (솔리드 마법 스크롤)
12. StarMap       (구체 성도/아스트롤라베)
13. Torch         (목재 손잡이 + 솔리드 불꽃)
14. TreasureKey   (슬림 황금 열쇠)
15. Whistle       (슬림 금속 호각)
"""

import asyncio
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
MESH_THRESHOLD = 0.70  # 미세 노이즈 및 파편 완전 차단을 위해 0.70 적용
TIMEOUT_SECONDS = 300

FAILED_15_ITEMS = [
    "AncientScroll", "BribeCoin", "BrokenCompass", "DivingHelmet", "FeatherCap",
    "ForgeHammer", "Glasses", "HuntingBow", "Leather", "PassDoc",
    "SpellScroll", "StarMap", "Torch", "TreasureKey", "Whistle"
]


def preprocess_and_filter_icon(item_id: str) -> str:
    """배경 제거 및 엣지 정제."""
    raw_icon = os.path.join(ICON_DIR, f"{item_id}.png")
    clean_icon = os.path.join(CLEAN_ICON_DIR, f"{item_id}_clean.png")

    if not os.path.exists(raw_icon):
        return ""

    img = Image.open(raw_icon)
    nobg = remove(img)
    
    # 순수 흰색 캔버스에 중앙 정렬
    white_bg = Image.new("RGB", nobg.size, (255, 255, 255))
    if nobg.mode == "RGBA":
        white_bg.paste(nobg, mask=nobg.split()[3])
    else:
        white_bg.paste(nobg)
        
    white_bg.save(clean_icon)
    return clean_icon


def postprocess_clean_mesh(glb_path: str):
    """메쉬 내 미세 부유 파편(면수 50 이하 찌꺼기)을 제거하여 솔리드 메인 바디만 보존."""
    try:
        scene = trimesh.load(glb_path)
        if isinstance(scene, trimesh.Scene):
            geoms = [g for g in scene.geometry.values() if isinstance(g, trimesh.Trimesh)]
            mesh = trimesh.util.concatenate(geoms) if geoms else None
        else:
            mesh = scene

        if mesh is None or len(mesh.vertices) == 0:
            return

        # 연결된 컴포넌트 분리 후 유의미한 컴포넌트(전체 면의 3% 이상 또는 상위 5개)만 병합
        comps = mesh.split(only_watertight=False)
        if len(comps) > 1:
            total_faces = len(mesh.faces)
            valid_comps = [c for c in comps if len(c.faces) > max(100, total_faces * 0.03)]
            if valid_comps:
                cleaned_mesh = trimesh.util.concatenate(valid_comps)
                cleaned_mesh.export(glb_path)
    except Exception as e:
        print(f"  [후처리 예외] {glb_path}: {e}")


async def upload_image(client: httpx.AsyncClient, file_path: str, upload_name: str) -> bool:
    try:
        with open(file_path, "rb") as f:
            files = {"image": (upload_name, f, "image/png")}
            data = {"overwrite": "true"}
            resp = await client.post(f"{COMFYUI_URL}/upload/image", files=files, data=data)
            return resp.status_code == 200
    except Exception:
        return False


def build_workflow(image_filename: str, item_id: str, seed: int) -> dict:
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
        "19": {"inputs": {"mesh": ["18", 0], "filename_prefix": f"3d/Refined_{item_id}"}, "class_type": "SaveGLB"}
    }


def render_3view(mesh_path: str, item_id: str, out_path: str):
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

        fig.suptitle(f"{item_id} (Refined v3) | Vertices: {len(verts):,} | Faces: {len(faces):,}", color="#ffffff", fontsize=13, y=0.98)
        plt.tight_layout()
        plt.savefig(out_path, dpi=90, facecolor=fig.get_facecolor(), edgecolor="none")
        plt.close(fig)
    except Exception as e:
        print(f"  [렌더 에러] {item_id}: {e}")


async def process_single(client: httpx.AsyncClient, item_id: str, index: int, total: int):
    out_glb = os.path.join(OUTPUT_DIR, f"{item_id}.glb")
    out_3view = os.path.join(SNAPSHOT_3VIEW_DIR, f"{item_id}_3view.png")

    clean_icon_path = preprocess_and_filter_icon(item_id)
    if not clean_icon_path:
        print(f"[{index}/{total}] ❌ {item_id}: 아이콘 누락")
        return False

    upload_name = f"{item_id}_clean.png"
    uploaded = await upload_image(client, clean_icon_path, upload_name)
    if not uploaded:
        return False

    seed = random.randint(1, 1000000000)
    workflow = build_workflow(upload_name, item_id, seed)

    print(f"[{index}/{total}] 🚀 3D 정밀 생성 시작: {item_id} (threshold=0.70)...")
    start_time = time.time()

    resp = await client.post(f"{COMFYUI_URL}/prompt", json={"prompt": workflow, "client_id": f"refine_{item_id}"})
    if resp.status_code != 200:
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
                        
                        # 메쉬 미세 파편 자동 필터링 후처리
                        postprocess_clean_mesh(out_glb)

                        elapsed = time.time() - start_time
                        size_mb = os.path.getsize(out_glb) / (1024 * 1024)
                        print(f"[{index}/{total}] ✅ 저장 및 파편 필터링 완료: {item_id} ({elapsed:.1f}초, {size_mb:.1f}MB)")

                        render_3view(out_glb, item_id, out_3view)
                        return True
    return False


async def main():
    print(f"=== [불합격 15종 전용 고품질 3D 재생성 및 정제 (threshold 0.70)] ===")
    total = len(FAILED_15_ITEMS)
    success = 0
    async with httpx.AsyncClient(timeout=TIMEOUT_SECONDS + 30) as client:
        for i, item_id in enumerate(FAILED_15_ITEMS, 1):
            if await process_single(client, item_id, i, total):
                success += 1

    print(f"\n=== 15종 정제 작업 완료: {success}/{total} 성공 ===")


if __name__ == "__main__":
    asyncio.run(main())


"""완전 파손 2종(CrystalBall, HonorFlower) 재생성.

기존 컴포넌트 청소로도 지배 조각이 없던(전체가 노이즈였던) 항목만
threshold 0.72 로 재생성 후 즉시 파편 후처리·재검증한다.
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
MESH_THRESHOLD = 0.72
TIMEOUT_SECONDS = 300

TARGET_ITEMS = ["CrystalBall", "HonorFlower"]
MIN_FRACTION = 0.005
MIN_FACES = 50
BROKEN_DOMINANT_THRESHOLD = 0.05


def preprocess_clean_icon(item_id: str) -> str:
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


def load_merged(path):
    scene = trimesh.load(path)
    if isinstance(scene, trimesh.Scene):
        geoms = [g for g in scene.geometry.values() if isinstance(g, trimesh.Trimesh)]
        return trimesh.util.concatenate(geoms) if geoms else None
    return scene


def cleanup_and_check(glb_path: str) -> dict:
    mesh = load_merged(glb_path)
    if mesh is None or len(mesh.vertices) == 0:
        return {"status": "EMPTY"}
    total_faces = len(mesh.faces)
    comps = mesh.split(only_watertight=False)
    comps_sorted = sorted(comps, key=lambda c: len(c.faces), reverse=True)
    top_fraction = len(comps_sorted[0].faces) / total_faces if comps_sorted else 0.0
    if top_fraction < BROKEN_DOMINANT_THRESHOLD:
        return {"status": "STILL_BROKEN", "top_fraction": round(top_fraction, 4), "components": len(comps)}
    keep = [c for c in comps if len(c.faces) >= max(MIN_FACES, total_faces * MIN_FRACTION)]
    cleaned = trimesh.util.concatenate(keep)
    cleaned.export(glb_path)
    return {
        "status": "OK",
        "components_before": len(comps),
        "components_kept": len(keep),
        "top_fraction": round(top_fraction, 4),
    }


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
        "13": {
            "inputs": {"image": ["10", 0], "clip_vision": ["11", 1], "crop": "center"},
            "class_type": "CLIPVisionEncode",
        },
        "12": {"inputs": {"clip_vision_output": ["13", 0]}, "class_type": "Hunyuan3Dv2Conditioning"},
        "14": {"inputs": {"resolution": LATENT_RESOLUTION, "batch_size": 1}, "class_type": "EmptyLatentHunyuan3Dv2"},
        "15": {"inputs": {"model": ["11", 0], "shift": SHIFT}, "class_type": "ModelSamplingAuraFlow"},
        "16": {
            "inputs": {
                "seed": seed,
                "steps": STEPS,
                "cfg": CFG,
                "sampler_name": SAMPLER,
                "scheduler": SCHEDULER,
                "denoise": 1.0,
                "model": ["15", 0],
                "positive": ["12", 0],
                "negative": ["12", 1],
                "latent_image": ["14", 0],
            },
            "class_type": "KSampler",
        },
        "17": {
            "inputs": {
                "samples": ["16", 0],
                "vae": ["11", 2],
                "num_chunks": NUM_CHUNKS,
                "octree_resolution": OCTREE_RESOLUTION,
            },
            "class_type": "VAEDecodeHunyuan3D",
        },
        "18": {
            "inputs": {"voxel": ["17", 0], "algorithm": MESH_ALGORITHM, "threshold": MESH_THRESHOLD},
            "class_type": "VoxelToMesh",
        },
        "19": {"inputs": {"mesh": ["18", 0], "filename_prefix": f"3d/Regen2_{item_id}"}, "class_type": "SaveGLB"},
    }


def render_3view(mesh_path: str, item_id: str, out_path: str):
    mesh = load_merged(mesh_path)
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
    render_faces = faces[:: max(1, len(faces) // 12000)] if len(faces) > 12000 else faces
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
    min_b, max_b = verts.min(axis=0), verts.max(axis=0)
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
    fig.suptitle(
        f"{item_id} (Regen2)  |  Vertices: {len(verts):,}  |  Faces: {len(faces):,}",
        color="#ffffff",
        fontsize=13,
        y=0.98,
    )
    plt.tight_layout()
    plt.savefig(out_path, dpi=90, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.close(fig)


async def process_one(client: httpx.AsyncClient, item_id: str, attempt: int) -> bool:
    out_glb = os.path.join(OUTPUT_DIR, f"{item_id}.glb")
    out_3view = os.path.join(SNAPSHOT_3VIEW_DIR, f"{item_id}_3view.png")

    clean_icon_path = preprocess_clean_icon(item_id)
    if not clean_icon_path:
        print(f"❌ {item_id}: 아이콘 누락")
        return False

    upload_name = f"{item_id}_regen2.png"
    if not await upload_image(client, clean_icon_path, upload_name):
        print(f"❌ {item_id}: 업로드 실패")
        return False

    seed = random.randint(1, 1000000000)
    workflow = build_workflow(upload_name, item_id, seed)
    print(f"🚀 {item_id} 재생성 시작 (attempt {attempt}, threshold={MESH_THRESHOLD})...")
    start_time = time.time()

    resp = await client.post(
        f"{COMFYUI_URL}/prompt", json={"prompt": workflow, "client_id": f"regen2_{item_id}_{attempt}"}
    )
    if resp.status_code != 200:
        print(f"❌ {item_id}: 큐 등록 실패 {resp.text}")
        return False
    prompt_id = resp.json().get("prompt_id")

    while time.time() - start_time < TIMEOUT_SECONDS:
        await asyncio.sleep(2.0)
        hist_resp = await client.get(f"{COMFYUI_URL}/history/{prompt_id}")
        if hist_resp.status_code != 200:
            continue
        hist_data = hist_resp.json()
        if prompt_id not in hist_data:
            continue
        outputs = hist_data[prompt_id].get("outputs", {})
        if "19" in outputs and "3d" in outputs["19"]:
            glb_info = outputs["19"]["3d"][0]
            glb_url = f"{COMFYUI_URL}/view?{urlencode(glb_info)}"
            glb_data = await client.get(glb_url)
            if glb_data.status_code != 200:
                return False
            with open(out_glb, "wb") as f:
                f.write(glb_data.content)
            elapsed = time.time() - start_time
            print(f"✅ {item_id}: GLB 저장 ({elapsed:.1f}s, {len(glb_data.content) / 1024 / 1024:.1f}MB)")

            check = cleanup_and_check(out_glb)
            print(f"   후처리 결과: {check}")
            if check["status"] != "OK":
                return False

            render_3view(out_glb, item_id, out_3view)
            print(f"📸 {item_id}: 3-view 스냅샷 갱신")
            return True

    print(f"⚠️ {item_id}: 타임아웃")
    return False


async def main():
    async with httpx.AsyncClient(timeout=TIMEOUT_SECONDS + 30) as client:
        for item_id in TARGET_ITEMS:
            ok = False
            for attempt in range(1, 4):
                ok = await process_one(client, item_id, attempt)
                if ok:
                    break
            print(f"=== {item_id}: {'성공' if ok else '3회 시도 후 실패'} ===\n")


if __name__ == "__main__":
    asyncio.run(main())

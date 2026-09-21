"""텍스처 적용 GLB 의 3방향 QA 프리뷰 렌더러.

페이스마다 UV 중심점의 텍스처 픽셀을 뽑아 면 색으로 쓰고, 노멀 기반 음영을
살짝 섞어 실제 착색 결과를 눈으로 확인할 수 있게 만든다.
(GPU 렌더러 없이 matplotlib 만으로 동작)
"""

import os
import sys

import numpy as np
import trimesh

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TEXTURED_DIR = os.path.join(REPO_ROOT, "Art/Meshes/Items_Textured")
OUT_DIR = os.path.join(REPO_ROOT, "Art/Meshes/Snapshots_Textured")
os.makedirs(OUT_DIR, exist_ok=True)

MAX_FACES = 20000


def load_textured(path):
    scene = trimesh.load(path)
    if isinstance(scene, trimesh.Scene):
        geoms = [g for g in scene.geometry.values() if isinstance(g, trimesh.Trimesh)]
        if not geoms:
            return None
        return geoms[0] if len(geoms) == 1 else trimesh.util.concatenate(geoms)
    return scene


def face_colors_from_texture(mesh):
    """각 면의 UV 중심에서 베이스컬러 텍스처를 샘플링해 면 색 배열을 만든다."""
    visual = mesh.visual
    tex = getattr(getattr(visual, "material", None), "baseColorTexture", None)
    if tex is None or not hasattr(visual, "uv") or visual.uv is None:
        return None

    img = np.asarray(tex.convert("RGB"), dtype=np.float32) / 255.0
    h, w = img.shape[:2]

    uv = np.asarray(visual.uv, dtype=np.float32)
    face_uv = uv[mesh.faces].mean(axis=1)  # (F, 2)

    # glTF UV 원점은 좌상단, 이미지 배열도 동일하므로 v 를 뒤집어 맞춘다.
    px = np.clip((face_uv[:, 0] * (w - 1)).astype(np.int32), 0, w - 1)
    py = np.clip(((1.0 - face_uv[:, 1]) * (h - 1)).astype(np.int32), 0, h - 1)
    return img[py, px]


def render(item_id: str, path: str) -> bool:
    mesh = load_textured(path)
    if mesh is None or len(mesh.faces) == 0:
        print(f"❌ {item_id}: 메시 로드 실패")
        return False

    base = face_colors_from_texture(mesh)
    if base is None:
        print(f"⚠️ {item_id}: 텍스처 없음 (UV/baseColorTexture 누락)")
        return False

    faces = mesh.faces
    if len(faces) > MAX_FACES:
        step = len(faces) // MAX_FACES
        idx = np.arange(0, len(faces), step)
    else:
        idx = np.arange(len(faces))

    tri = mesh.vertices[faces[idx]]
    cols = base[idx]

    v0, v1, v2 = tri[:, 0], tri[:, 1], tri[:, 2]
    normals = np.cross(v1 - v0, v2 - v0)
    normals /= np.linalg.norm(normals, axis=1, keepdims=True) + 1e-8
    light = np.array([0.4, 0.5, 1.0])
    light /= np.linalg.norm(light)
    shade = np.clip(np.abs(np.dot(normals, light)), 0.45, 1.0)[:, None]

    rgba = np.ones((len(idx), 4))
    rgba[:, :3] = np.clip(cols * (0.55 + 0.45 * shade), 0, 1)

    fig = plt.figure(figsize=(15, 5), dpi=95, facecolor="#14141e")
    views = [
        {"title": "Front View", "elev": 0, "azim": 0, "pos": 131},
        {"title": "Side View", "elev": 0, "azim": 90, "pos": 132},
        {"title": "Isometric View", "elev": 25, "azim": 45, "pos": 133},
    ]

    verts = mesh.vertices
    min_b, max_b = verts.min(axis=0), verts.max(axis=0)
    center = (min_b + max_b) / 2.0
    rng = (max_b - min_b).max() / 2.0

    for v in views:
        ax = fig.add_subplot(v["pos"], projection="3d", facecolor="#14141e")
        ax.set_axis_off()
        ax.add_collection3d(Poly3DCollection(tri, facecolors=rgba, edgecolors="none", shade=False))
        ax.set_xlim(center[0] - rng, center[0] + rng)
        ax.set_ylim(center[1] - rng, center[1] + rng)
        ax.set_zlim(center[2] - rng, center[2] + rng)
        ax.view_init(elev=v["elev"], azim=v["azim"])
        ax.set_title(v["title"], color="#b0b0c0", fontsize=10, pad=-2)

    fig.suptitle(
        f"{item_id} (Textured)  |  V: {len(verts):,}  |  F: {len(faces):,}", color="#ffffff", fontsize=13, y=0.98
    )
    plt.tight_layout()
    out_path = os.path.join(OUT_DIR, f"{item_id}_textured.png")
    plt.savefig(out_path, dpi=95, facecolor=fig.get_facecolor(), edgecolor="none")
    plt.close(fig)
    print(f"✅ {item_id}: {out_path}")
    return True


def main(targets):
    for item_id in targets:
        path = os.path.join(TEXTURED_DIR, f"{item_id}.glb")
        if not os.path.exists(path):
            print(f"❌ {item_id}: {path} 없음")
            continue
        render(item_id, path)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] != "all":
        items = [t.strip() for t in sys.argv[1].split(",")]
    else:
        items = sorted(os.path.splitext(f)[0] for f in os.listdir(TEXTURED_DIR) if f.endswith(".glb"))
    main(items)

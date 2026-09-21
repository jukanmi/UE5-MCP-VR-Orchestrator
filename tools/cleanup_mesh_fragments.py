"""파편(floating debris) 청소기.

72종 GLB 각각을 connected-component 로 분리해, 전체 면적 대비 유의미한
비중(기본 0.5%, 최소 50 faces)을 가진 조각만 남기고 나머지 먼지를 버린다.
청소 후에도 지배적 조각(최대 컴포넌트가 전체의 5% 미만)이 없으면
'파손' 으로 분류해 재생성 대상으로 남긴다 (덮어쓰지 않음).
"""

import glob
import json
import os

import trimesh

MESH_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../Art/Meshes/Items"))
REPORT_FILE = os.path.abspath(os.path.join(os.path.dirname(__file__), "../Art/Meshes/cleanup_report.json"))

MIN_FRACTION = 0.005  # 0.5%
MIN_FACES = 50
BROKEN_DOMINANT_THRESHOLD = 0.05  # 최대 조각이 전체의 5% 미만이면 파손 판정


def load_merged(path):
    scene = trimesh.load(path)
    if isinstance(scene, trimesh.Scene):
        geoms = [g for g in scene.geometry.values() if isinstance(g, trimesh.Trimesh)]
        return trimesh.util.concatenate(geoms) if geoms else None
    return scene


def cleanup_one(item_id, path):
    mesh = load_merged(path)
    if mesh is None or len(mesh.vertices) == 0:
        return {"item_id": item_id, "status": "EMPTY"}

    total_faces_before = len(mesh.faces)
    comps = mesh.split(only_watertight=False)
    comps_sorted = sorted(comps, key=lambda c: len(c.faces), reverse=True)
    top_fraction = len(comps_sorted[0].faces) / total_faces_before if comps_sorted else 0.0

    if top_fraction < BROKEN_DOMINANT_THRESHOLD:
        return {
            "item_id": item_id,
            "status": "BROKEN_NEEDS_REGEN",
            "components_before": len(comps),
            "top_fraction": round(top_fraction, 4),
            "faces_before": total_faces_before,
        }

    keep = [c for c in comps if len(c.faces) >= max(MIN_FACES, total_faces_before * MIN_FRACTION)]
    cleaned = trimesh.util.concatenate(keep)
    cleaned.export(path)

    return {
        "item_id": item_id,
        "status": "CLEANED",
        "components_before": len(comps),
        "components_kept": len(keep),
        "faces_before": total_faces_before,
        "faces_after": len(cleaned.faces),
        "top_fraction": round(top_fraction, 4),
    }


def main():
    glb_files = sorted(glob.glob(os.path.join(MESH_DIR, "*.glb")))
    results = []
    for i, path in enumerate(glb_files, 1):
        item_id = os.path.splitext(os.path.basename(path))[0]
        try:
            r = cleanup_one(item_id, path)
        except Exception as e:
            r = {"item_id": item_id, "status": "ERROR", "error": str(e)}
        results.append(r)
        print(
            f"[{i:02d}/{len(glb_files)}] {item_id}: {r['status']}"
            + (f" ({r.get('components_before')}->{r.get('components_kept')} comps)" if r["status"] == "CLEANED" else "")
        )

    with open(REPORT_FILE, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)

    broken = [r["item_id"] for r in results if r["status"] == "BROKEN_NEEDS_REGEN"]
    print(f"\n=== 청소 완료: {len(results)}개 처리, 재생성 필요 {len(broken)}개 ===")
    print("재생성 대상:", ", ".join(broken) if broken else "없음")


if __name__ == "__main__":
    main()

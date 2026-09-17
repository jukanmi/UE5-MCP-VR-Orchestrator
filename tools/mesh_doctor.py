"""Mesh Doctor — 3D 에셋 무결성 진단 및 치료 도구 (SoL-Pi 하네스 연동)

온디맨드(On-Demand) 호출 방식으로 3D 메시의 결함을 진단하고 치료합니다:
1. diagnose : 지오메트리 결함(플로팅 파편, 비다양체, 면 뒤집힘, 과도한 폴리곤, 스케일 이상) 슬라이싱 리포트.
2. heal     : 결함 메시 자동 치료 (파편 제거, 노멀 정렬, 퇴화 면 제거, PBR 텍스처 100% 보존).
3. ue-audit : UE5 에디터 내부(StaticMesh 콜리전, Nanite, 4K 텍스처 과부하) 진단 스크립트 생성/출력.
"""

import os
import sys
import argparse
import glob
from pathlib import Path
from typing import List, Optional, Tuple

# Windows 콘솔 UTF-8 출력 강제
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

try:
    import numpy as np
    import trimesh
except ImportError as e:
    print(f"❌ [MeshDoctor] 필수 라이브러리가 설치되지 않았습니다: {e}")
    print("pip install trimesh numpy 를 실행하세요.")
    sys.exit(1)

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MESH_DIR = PROJECT_ROOT / "Art" / "Meshes" / "Items_Textured"

# VR 최적화 기준 예산 (단일 프랍/아이템 기준)
VR_FACE_BUDGET_WARN = 10000    # 경고 (노란색)
VR_FACE_BUDGET_DANGER = 25000  # 위험 (빨간색)
MIN_COMPONENT_FACES = 15       # 이 페이스 미만의 연결 컴포넌트는 부유 먼지/파편으로 간주


class MeshDiagnosis:
    def __init__(self, file_path: str):
        self.file_path = file_path
        self.filename = os.path.basename(file_path)
        self.total_faces = 0
        self.total_verts = 0
        self.is_watertight = False
        self.is_winding_consistent = False
        self.num_components = 0
        self.floater_faces = 0
        self.floater_ratio = 0.0
        self.extents = [0.0, 0.0, 0.0]  # x, y, z 크기
        self.issues: List[str] = []
        self.status = "OK"

    def is_healthy(self) -> bool:
        return len(self.issues) == 0


def diagnose_mesh(file_path: str) -> MeshDiagnosis:
    """단일 3D 파일(GLB/OBJ)의 기하학적 결함 진단"""
    diag = MeshDiagnosis(file_path)
    try:
        scene_or_mesh = trimesh.load(file_path, force="scene")
        if isinstance(scene_or_mesh, trimesh.Scene):
            geoms = [g for g in scene_or_mesh.geometry.values() if isinstance(g, trimesh.Trimesh)]
            if not geoms:
                diag.issues.append("EMPTY_GEOMETRY")
                diag.status = "BROKEN"
                return diag
            # 진단용으로 가장 주요한 지오메트리 선택
            mesh = geoms[0]
        else:
            mesh = scene_or_mesh

        diag.total_faces = len(mesh.faces)
        diag.total_verts = len(mesh.vertices)
        diag.is_watertight = bool(mesh.is_watertight)
        diag.is_winding_consistent = bool(mesh.is_winding_consistent)
        diag.extents = [round(float(x), 3) for x in mesh.extents]

        # 1. 연결 컴포넌트(부유 파편) 진단
        if len(mesh.faces) > 0 and len(mesh.face_adjacency) > 0:
            comps = trimesh.graph.connected_components(mesh.face_adjacency, min_len=1)
            diag.num_components = len(comps)
            if diag.num_components > 5:
                # 상위 3개 컴포넌트를 제외한 파편 계산
                sorted_comps = sorted(comps, key=len, reverse=True)
                dominant_faces = sum(len(c) for c in sorted_comps[:3])
                debris_faces = diag.total_faces - dominant_faces
                diag.floater_faces = debris_faces
                diag.floater_ratio = debris_faces / max(1, diag.total_faces)

                if diag.num_components > 20 or diag.floater_ratio > 0.05:
                    diag.issues.append(f"FLOATERS({diag.num_components}조각, 파편{diag.floater_ratio*100:.1f}%)")

        # 2. 노멀 및 방향성 불일치
        if not diag.is_winding_consistent:
            diag.issues.append("INVERTED_NORMALS(면뒤집힘)")

        # 3. 방수(Watertight) 여부
        if not diag.is_watertight:
            diag.issues.append("NON_WATERTIGHT(구멍/틈새)")

        # 4. 폴리곤 예산 초과 여부
        if diag.total_faces >= VR_FACE_BUDGET_DANGER:
            diag.issues.append(f"HIGH_POLY_DANGER({diag.total_faces:,} tris)")
        elif diag.total_faces >= VR_FACE_BUDGET_WARN:
            diag.issues.append(f"HIGH_POLY_WARN({diag.total_faces:,} tris)")

        # 5. 스케일 이상치 (VR 아이템 기준: 한 축이 10m 초과이거나 1mm 미만)
        max_dim = max(diag.extents) if diag.extents else 0.0
        min(diag.extents) if diag.extents else 0.0
        if max_dim > 10.0:
            diag.issues.append(f"SCALE_TOO_LARGE({max_dim}m)")
        elif max_dim < 0.01 and max_dim > 0:
            diag.issues.append(f"SCALE_TOO_SMALL({max_dim*100:.1f}cm)")

        # 상태 판정
        if any("DANGER" in i or "BROKEN" in i for i in diag.issues):
            diag.status = "DANGER"
        elif diag.issues:
            diag.status = "WARN"
        else:
            diag.status = "HEALTHY"

    except Exception as ex:
        diag.issues.append(f"LOAD_ERROR({str(ex)})")
        diag.status = "ERROR"

    return diag


def run_diagnose(target_path: str) -> int:
    """대상 경로의 파일들을 진단하고 컴팩트하게 슬라이싱 출력"""
    path = Path(target_path)
    if path.is_file():
        files = [str(path)]
    elif path.is_dir():
        files = sorted(glob.glob(str(path / "*.glb")) + glob.glob(str(path / "*.obj")))
    else:
        print(f"❌ [MeshDoctor] 유효하지 않은 경로: {target_path}")
        return 1

    if not files:
        print(f"⚠️ [MeshDoctor] 검사할 3D 파일이 없습니다: {target_path}")
        return 0

    print(f"\n🔍 [MeshDoctor] 3D 메시 정밀 진단 시작 (총 {len(files)}개 에셋)...")
    print("=" * 70)

    healthy_count = 0
    warn_count = 0
    danger_count = 0
    results: List[MeshDiagnosis] = []

    for f in files:
        diag = diagnose_mesh(f)
        results.append(diag)
        if diag.status == "HEALTHY":
            healthy_count += 1
        elif diag.status == "WARN":
            warn_count += 1
        else:
            danger_count += 1

    # 결함이 있는 에셋만 슬라이싱 출력
    problem_items = [d for d in results if not d.is_healthy()]
    if problem_items:
        print(f"⚠️ [결함 감지] 치료가 필요한 에셋 {len(problem_items)}건:\n")
        for d in problem_items:
            icon = "🔴" if d.status in ("DANGER", "ERROR") else "🟡"
            issues_str = " | ".join(d.issues)
            ext_str = f"{d.extents[0]}x{d.extents[1]}x{d.extents[2]}m"
            print(f" {icon} {d.filename:<24} | {d.total_faces:>6,} tris | {ext_str:<15} | {issues_str}")
    else:
        print("✅ 모든 3D 에셋이 결함 없이 건강합니다!")

    print("=" * 70)
    print(f"📊 [요약] 전체: {len(files)} | 건강: {healthy_count} | 경고: {warn_count} | 위험/오류: {danger_count}")
    if problem_items:
        print(f"💡 치료 명령: python tools/mesh_doctor.py heal \"{target_path}\"")
    print()
    return 0 if danger_count == 0 else 1


def heal_mesh(file_path: str, backup: bool = False, max_faces: Optional[int] = None) -> Tuple[bool, str]:
    """텍스처/머티리얼을 100% 보존하면서 지오메트리 결함(파편, 노멀, 퇴화면) 치료"""
    try:
        scene = trimesh.load(file_path, force="scene")
        if not isinstance(scene, trimesh.Scene) or len(scene.geometry) == 0:
            return False, "빈 씬이거나 지원되지 않는 형식"

        if backup:
            backup_path = file_path + ".bak"
            if not os.path.exists(backup_path):
                import shutil
                shutil.copy2(file_path, backup_path)

        modified = False
        report_details = []

        for name, geom in scene.geometry.items():
            if not isinstance(geom, trimesh.Trimesh):
                continue

            orig_faces = len(geom.faces)
            if orig_faces == 0:
                continue

            # 1. 연결 컴포넌트 분석 및 부유 파편 마스킹 (텍스처 보존 방식)
            if len(geom.face_adjacency) > 0:
                comps = trimesh.graph.connected_components(geom.face_adjacency, min_len=1)
                if len(comps) > 1:
                    # 유의미한 크기의 컴포넌트만 유지
                    sorted_comps = sorted(comps, key=len, reverse=True)
                    top_faces = len(sorted_comps[0])
                    min_keep_faces = max(MIN_COMPONENT_FACES, int(orig_faces * 0.005))

                    keep_face_indices = set()
                    for c in sorted_comps:
                        if len(c) >= min_keep_faces or len(c) == top_faces:
                            keep_face_indices.update(c)

                    if len(keep_face_indices) < orig_faces:
                        mask = np.zeros(orig_faces, dtype=bool)
                        for idx in keep_face_indices:
                            mask[idx] = True
                        geom.update_faces(mask)
                        removed = orig_faces - len(geom.faces)
                        report_details.append(f"파편 제거({removed}면)")
                        modified = True

            # 2. 중복 페이스 / 퇴화된 면 / 미참조 정점 정리
            try:
                geom.update_faces(geom.unique_faces())
                geom.update_faces(geom.nondegenerate_faces())
                geom.remove_unreferenced_vertices()
            except Exception:
                pass

            # 3. 노멀 및 권선 방향 치유
            try:
                trimesh.repair.fix_normals(geom)
                trimesh.repair.fix_winding(geom)
            except Exception:
                pass

            # 4. 선택적 폴리곤 감축 (지정 시)
            if max_faces and len(geom.faces) > max_faces:
                before_dec = len(geom.faces)
                try:
                    geom = geom.simplify_quadric_decimation(max_faces)
                    scene.geometry[name] = geom
                    report_details.append(f"폴리감축({before_dec}->{len(geom.faces)})")
                    modified = True
                except Exception:
                    pass

        if modified:
            # GLB 포맷으로 완벽 익스포트
            scene.export(file_path)
            return True, ", ".join(report_details) if report_details else "정리 완료"
        else:
            return True, "이미 최적 상태(수정 불필요)"

    except Exception as ex:
        return False, f"치료 실패: {str(ex)}"


def run_heal(target_path: str, backup: bool = True, max_faces: Optional[int] = None) -> int:
    """대상 경로의 메시 결함 자동 치료 실행"""
    path = Path(target_path)
    if path.is_file():
        files = [str(path)]
    elif path.is_dir():
        files = sorted(glob.glob(str(path / "*.glb")) + glob.glob(str(path / "*.obj")))
    else:
        print(f"❌ [MeshDoctor] 유효하지 않은 경로: {target_path}")
        return 1

    print(f"\n🩺 [MeshDoctor] 3D 메시 자동 치료 시작 ({len(files)}개 대상)...")
    print("=" * 70)

    success_count = 0
    fail_count = 0

    for f in files:
        fname = os.path.basename(f)
        success, msg = heal_mesh(f, backup=backup, max_faces=max_faces)
        if success:
            success_count += 1
            print(f" ✅ {fname:<24} | {msg}")
        else:
            fail_count += 1
            print(f" ❌ {fname:<24} | {msg}")

    print("=" * 70)
    print(f"📊 [치료 완료] 성공: {success_count} | 실패: {fail_count}\n")
    return 0 if fail_count == 0 else 1


def get_ue_audit_script() -> str:
    """언리얼 엔진 에디터 내부(Python/MCP)에서 실행할 에셋 프로파일링 스크립트 반환"""
    return '''# [UE5 In-Engine Asset Audit Script]
import unreal

dest_path = "/Game/Core/Mesh/Items"
assets = unreal.EditorAssetLibrary.list_assets(dest_path, recursive=True)

print("=== [MeshDoctor UE5 Audit] StaticMesh 무결성 및 VR 최적화 검사 ===")
missing_collision = []
high_poly_no_nanite = []

for asset_path in assets:
    obj = unreal.EditorAssetLibrary.load_asset(asset_path)
    if isinstance(obj, unreal.StaticMesh):
        name = obj.get_name()
        
        # 1. 콜리전 검사 (VR 물리 그립/던지기에 필수)
        body_setup = obj.get_editor_property("body_setup")
        has_collision = False
        if body_setup:
            agg_geom = body_setup.get_editor_property("agg_geom")
            sphere_cnt = len(agg_geom.get_editor_property("sphere_elems"))
            box_cnt = len(agg_geom.get_editor_property("box_elems"))
            sphyl_cnt = len(agg_geom.get_editor_property("sphyl_elems"))
            convex_cnt = len(agg_geom.get_editor_property("convex_elems"))
            has_collision = (sphere_cnt + box_cnt + sphyl_cnt + convex_cnt) > 0
            
        if not has_collision:
            missing_collision.append(name)
            
        # 2. Nanite 검사
        nanite_settings = obj.get_editor_property("nanite_settings")
        is_nanite = nanite_settings.get_editor_property("enabled")
        num_triangles = obj.get_num_triangles(0)
        
        if num_triangles > 15000 and not is_nanite:
            high_poly_no_nanite.append((name, num_triangles))

print(f"총 검사 에셋: {len(assets)}개")
if missing_collision:
    print(f"⚠️ [물리 콜리전 누락 - VR 바닥 통과 위험] {len(missing_collision)}개:")
    for m in missing_collision:
        print(f"  - {m}")
else:
    print("✅ 모든 스태틱 메시에 단순 콜리전이 정상 설정되어 있습니다.")

if high_poly_no_nanite:
    print(f"⚠️ [하이폴리 Nanite 미적용] {len(high_poly_no_nanite)}개:")
    for m, tris in high_poly_no_nanite:
        print(f"  - {m} ({tris:,} tris)")
print("=== [검사 종료] ===")
'''


def run_ue_audit() -> int:
    """UE5 감사 스크립트를 출력하고 안내"""
    script = get_ue_audit_script()
    print("\n📦 [MeshDoctor] 언리얼 엔진 에디터용 검사 스크립트:")
    print("=" * 70)
    print(script)
    print("=" * 70)
    print("💡 에디터 실행 중일 때 MCP 도구 `ue_run_python`에 위 코드를 전달하여 즉시 검사할 수 있습니다.\n")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Mesh Doctor — 3D 에셋 무결성 진단 및 치료 도구")
    subparsers = parser.add_subparsers(dest="command", help="실행할 작업")

    # diagnose
    diag_parser = subparsers.add_parser("diagnose", aliases=["check"], help="3D 에셋 지오메트리 결함 진단")
    diag_parser.add_argument("path", nargs="?", default=str(DEFAULT_MESH_DIR), help="검사할 파일 또는 폴더 경로")

    # heal
    heal_parser = subparsers.add_parser("heal", aliases=["fix"], help="3D 에셋 결함 자동 치료")
    heal_parser.add_argument("path", nargs="?", default=str(DEFAULT_MESH_DIR), help="치료할 파일 또는 폴더 경로")
    heal_parser.add_argument("--no-backup", action="store_true", help="원본 백업(.bak) 생략")
    heal_parser.add_argument("--max-faces", type=int, default=None, help="최대 허용 폴리곤 수 (초과 시 감축)")

    # ue-audit
    subparsers.add_parser("ue-audit", help="UE5 에디터 내부 StaticMesh 콜리전/Nanite 진단")

    args = parser.parse_args()

    if not args.command or args.command in ("diagnose", "check"):
        path = getattr(args, "path", str(DEFAULT_MESH_DIR))
        sys.exit(run_diagnose(path))
    elif args.command in ("heal", "fix"):
        backup = not args.no_backup
        sys.exit(run_heal(args.path, backup=backup, max_faces=args.max_faces))
    elif args.command == "ue-audit":
        sys.exit(run_ue_audit())
    else:
        parser.print_help()


if __name__ == "__main__":
    main()

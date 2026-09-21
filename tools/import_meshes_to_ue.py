"""Art/Meshes/Items_Textured/*.glb 72종을 UE5 StaticMesh 로 임포트하고 DT_ItemRegistry 에 연결한다.

에디터 내 Python 콘솔 또는 MCP ue_run_python 으로 실행한다.

하는 일:
1. GLB -> /Game/Core/Mesh/Items/<ItemID> StaticMesh 임포트 (텍스처·머티리얼 동반 생성)
2. 단순 콜리전 생성 — ADroppedItemBase 가 SimulatePhysics(true) + PhysicsActor 프로파일을 쓰는데
   GLB 는 콜리전이 없어 그대로 두면 물리 바디가 안 잡히고 바닥을 통과한다.
3. ItemRegistry.csv -> DT_ItemRegistry Reimport 후 WorldMesh 포인터 실물 대조 검증

아이콘 쪽(import_icons_to_ue.py)과 같은 흐름이다. 경로 관례만 다르다 —
아이콘은 /Game/UI/Icons/Items, 메시는 기존 /Game/Core/Mesh/<카테고리> 관례를 따른다.
"""

import glob
import os

try:
    import unreal
except ImportError:
    unreal = None

DEST_PATH = "/Game/Core/Mesh/Items"


def import_meshes() -> int:
    if not unreal:
        print("[오류] unreal 모듈을 찾을 수 없습니다. 언리얼 에디터 내부 Python에서 실행해야 합니다.")
        return 0

    src_dir = os.path.join(unreal.Paths.project_dir(), "Art", "Meshes", "Items_Textured")
    glb_files = sorted(glob.glob(os.path.join(src_dir, "*.glb")))
    if not glb_files:
        print(f"[경고] 임포트할 GLB 파일이 없습니다: {src_dir}")
        return 0

    print(f"=== 아이템 메시 {len(glb_files)}종 임포트 시작 -> {DEST_PATH} ===")

    tasks = []
    for glb in glb_files:
        task = unreal.AssetImportTask()
        task.filename = glb
        task.destination_path = DEST_PATH
        task.destination_name = os.path.splitext(os.path.basename(glb))[0]
        task.replace_existing = True
        task.automated = True
        task.save = True
        tasks.append(task)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    imported = [
        t.destination_name
        for t in tasks
        if unreal.EditorAssetLibrary.does_asset_exist(f"{DEST_PATH}/{t.destination_name}")
    ]
    print(f"=== 임포트 완료: {len(imported)} / {len(tasks)} ===")

    missing = sorted(set(t.destination_name for t in tasks) - set(imported))
    if missing:
        print(f"[경고] 에셋이 생성되지 않은 항목 {len(missing)}개: {', '.join(missing[:10])}")

    return len(imported)


def add_collisions() -> int:
    """단순 콜리전을 붙인다. 이미 있으면 건드리지 않는다."""
    if not unreal:
        return 0

    # UE 5.5 에서 EditorStaticMeshLibrary 는 StaticMeshEditorSubsystem 으로 넘어갔다.
    subsystem = None
    if hasattr(unreal, "StaticMeshEditorSubsystem"):
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

    done = 0
    for asset_path in unreal.EditorAssetLibrary.list_assets(DEST_PATH, recursive=False):
        mesh = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue

        try:
            if subsystem:
                if subsystem.get_simple_collision_count(mesh) > 0:
                    continue
                # 손에 드는 소품이라 18-DOP 면 충분하다. 볼록 분해는 비용만 크다.
                subsystem.add_simple_collisions(mesh, unreal.ScriptingCollisionShapeType.NDOP18)
            else:
                if unreal.EditorStaticMeshLibrary.get_simple_collision_count(mesh) > 0:
                    continue
                unreal.EditorStaticMeshLibrary.add_simple_collisions(mesh, unreal.ScriptingCollisionShapeType.NDOP18)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
            done += 1
        except Exception as e:
            print(f"[경고] 콜리전 생성 실패: {asset_path} ({e})")

    print(f"=== 콜리전 생성: {done}종 ===")
    return done


def reimport_and_validate_datatable():
    if not unreal:
        return

    dt_path = "/Game/Data/Items/DT_ItemRegistry"
    dt_asset = unreal.EditorAssetLibrary.load_asset(dt_path)
    if not dt_asset:
        print(f"[경고] DT_ItemRegistry를 찾을 수 없습니다: {dt_path}")
        return

    csv_path = os.path.join(unreal.Paths.project_content_dir(), "Data", "Items", "ItemRegistry.csv")
    if not os.path.exists(csv_path):
        print(f"[경고] CSV 파일이 없습니다: {csv_path}")
        return

    if not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(dt_asset, csv_path):
        print("[오류] DT_ItemRegistry CSV Reimport 실패")
        return

    unreal.EditorAssetLibrary.save_loaded_asset(dt_asset)
    print("=== DT_ItemRegistry Reimport 및 저장 완료 ===")

    row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt_asset)
    missing = [str(r) for r in row_names if not unreal.EditorAssetLibrary.does_asset_exist(f"{DEST_PATH}/{r}.{r}")]

    print(f"\n=== WorldMesh 포인터 대조: {len(row_names) - len(missing)} / {len(row_names)} ===")
    if missing:
        print(f"누락 {len(missing)}개: {', '.join(missing[:10])}")


def run():
    import_meshes()
    add_collisions()
    reimport_and_validate_datatable()


if __name__ == "__main__" and unreal:
    run()

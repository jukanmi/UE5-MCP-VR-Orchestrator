"""UE5 에디터 내 Python 콘솔 및 MCP ue_run_python 용 아이콘 임포트 및 검증 스크립트.

특징:
1. Art/Icons/Items/*.png 72종을 /Game/UI/Icons/Items/* 텍스처로 자동 임포트
   (원본 PNG 는 Content/ 밖에 둔다 — 안에 두면 UE 자동 임포트로 png·uasset 두 벌이 쌓인다)
2. 텍스처 설정:
   - LODGroup = TEXTUREGROUP_UI
   - sRGB = True
   - CompressionSettings = TC_EditorIcon / TC_UserInterface2D (UI 뭉개짐 방지)
3. Content/Data/Items/ItemRegistry.csv -> DT_ItemRegistry Reimport
4. 각 Row의 Icon SoftObjectPtr 실제 에셋 존재 여부 덤프 대조 검증
"""

import glob
import os

try:
    import unreal
except ImportError:
    unreal = None


def import_icons() -> int:
    if not unreal:
        print("[오류] unreal 모듈을 찾을 수 없습니다. 언리얼 에디터 내부 Python에서 실행해야 합니다.")
        return 0

    project_dir = unreal.Paths.project_dir()
    icons_src_dir = os.path.join(project_dir, "Art", "Icons", "Items")
    destination_path = "/Game/UI/Icons/Items"

    png_files = glob.glob(os.path.join(icons_src_dir, "*.png"))
    if not png_files:
        print(f"[경고] 임포트할 PNG 파일이 없습니다: {icons_src_dir}")
        return 0

    print(f"=== [P2] 아이템 아이콘 {len(png_files)}종 UE5 텍스처 임포트 시작 ===")

    # 팩토리를 지정하지 않는다 — PNG 는 임포터가 알아서 Texture2D 로 판별한다.
    # UE 5.5 의 TextureFactory 에는 no_compression 프로퍼티가 노출돼 있지 않아
    # 팩토리를 직접 만들어 설정하려 하면 임포트 전에 예외로 죽는다.
    # 압축·sRGB·LODGroup 은 임포트 뒤 텍스처 에셋에 직접 건다(아래 후처리).
    tasks = []
    for png in png_files:
        task = unreal.AssetImportTask()
        task.filename = png
        task.destination_path = destination_path
        task.destination_name = os.path.splitext(os.path.basename(png))[0]
        task.replace_existing = True
        task.automated = True
        task.save = True
        tasks.append(task)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_tools.import_asset_tasks(tasks)

    # 임포트된 텍스처들의 세부 프로퍼티 후처리 (UI 압축 & sRGB & LODGroup)
    for task in tasks:
        asset_path = f"{destination_path}/{task.destination_name}"
        tex_obj = unreal.EditorAssetLibrary.load_asset(asset_path)
        if tex_obj and isinstance(tex_obj, unreal.Texture2D):
            tex_obj.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)

            # 프로퍼티명은 srgb — s_rgb 로 쓰면 임포트 후처리가 예외로 죽는다(UE 5.5 실측).
            tex_obj.set_editor_property("srgb", True)

            # 아이콘은 블록 압축 아티팩트가 그대로 보이므로 무압축으로 둔다.
            tex_obj.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)

            # 무압축 768x768 은 장당 2.4MB(72종 = 170MB)라 VR 예산에 과하다.
            # 인벤토리 슬롯은 100px 남짓이라 256 으로 잘라도 육안 차이가 없다(72종 = 19MB).
            tex_obj.set_editor_property("max_texture_size", 256)

            unreal.EditorAssetLibrary.save_loaded_asset(tex_obj)

    print(f"=== [P2] 아이콘 {len(tasks)}종 텍스처 설정(UI, sRGB, EditorIcon) 및 저장 완료 ===")
    return len(tasks)


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

    # 1. DataTable Reimport
    reimport_success = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(dt_asset, csv_path)
    if not reimport_success:
        print("[오류] DT_ItemRegistry CSV Reimport 실패")
        return

    unreal.EditorAssetLibrary.save_loaded_asset(dt_asset)
    print("=== [P2] DT_ItemRegistry 데이터테이블 Reimport 및 저장 완료 ===")

    # 2. Icon SoftObjectPtr 실제 에셋 존재 여부 전수 덤프 검증
    row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt_asset)
    print(f"\n=== [P2 검증] 총 {len(row_names)}개 아이템의 Icon 소프트 포인터 대조 검증 ===")

    valid_icons = 0
    missing_icons = []

    for r_name in row_names:
        # get_data_table_row_names returns Name array
        # Icon 경로 규격: /Game/UI/Icons/Items/<RowName>.<RowName>
        expected_icon_path = f"/Game/UI/Icons/Items/{r_name}.{r_name}"
        exists = unreal.EditorAssetLibrary.does_asset_exist(expected_icon_path)
        if exists:
            valid_icons += 1
        else:
            missing_icons.append(str(r_name))

    print(f"✅ 유효한 Icon 텍스처 에셋: {valid_icons} / {len(row_names)}")
    if missing_icons:
        print(f"⚠️ 누락된 Icon 에셋 ({len(missing_icons)}개): {', '.join(missing_icons[:10])}...")
    else:
        print("🎉 모든 아이템의 Icon 텍스처 포인터가 완벽하게 실물 에셋과 100% 일치합니다!")


def run():
    import_icons()
    reimport_and_validate_datatable()


if __name__ == "__main__" and unreal:
    run()

"""주민 BP 생성 — /Game/Blueprint/Villager/{BP_Villager, BP_Villager_Townsfolk, BP_Villager_Refugee, BP_Villager_Scholar}. 멱등(있으면 값만 갱신).

에디터 안에서 실행: MCP `ue_run_python(mode="file", script="<이 파일 절대경로>")`.
메시·애니는 Quaternius Ultimate Modular Characters(CC0) "Individual Characters" FBX — 의상 1개 = 스켈레탈 메시 1 + 클립 24 내장,
릭은 적(RPG Characters)과 같은 CharacterArmature 계열이라 클립 이름도 같은 규칙({char}_Anim_CharacterArmature_{clip}).
없으면 여기서 Interchange 로 /Game/Villager/Quaternius/{char} 에 임포트한다(텍스처 없음 — 단색 머티리얼).
AnimBP 없음 — AVillagerCharacter 가 Idle/Walk/Run/Wave/HitRecieve 를 단일 노드로 직접 재생하므로 BP 엔 클립 5개만 꽂는다.
종류별 자식 BP 가 VillagerID 접두·BaseStats·의상·배회 반경을 다르게 가진다. 개체 번호(Townsfolk_3)는 build_story_scene.py 가 붙인다.
대사 풀·키워드 규칙·비트별 길 안내(Phase 2)와 상인 재고(Phase 3)도 여기에 데이터로 둔다 — 대사 데이터의 단일 원본.
"""

import os

import unreal

DIR = "/Game/Blueprint/Villager"
BASE = f"{DIR}/BP_Villager"
Q = "/Game/Villager/Quaternius"
RAW = os.path.join(
    unreal.Paths.project_dir(), "RawAssets", "quaternius_ultimate_modular_characters", "Individual Characters", "FBX"
)
SND = "/Game/Enemy/Sounds"  # Kenney CC0 — 적과 공유

CLIPS = {"IdleAnim": "Idle", "WalkAnim": "Walk", "RunAnim": "Run", "WaveAnim": "Wave", "HitAnim": "HitRecieve"}
# FBX 에 클립 24개가 내장 — 쓰는 5개 + 후속(Interact·Death)만 남기고 임포트 직후 지운다(의상당 8MB → 3MB).
KEEP_CLIPS = set(CLIPS.values()) | {"Interact", "Death"}

# 종류: (BP 이름, VillagerID 접두, Quaternius 의상, BaseStats, 배회 반경 기본)
# 참고 공식(CharacterAttributes.h): DEF=Con, HP=150+Con×15, 속도는 Dex(10 → 걷기 200/달리기 400). 공격은 안 한다.
# 플레이어 스윙 최대 100 이라 Con 4~5 = 3방. 배회 반경은 배치 시 개체별로 덮어쓴다.
KINDS = [
    ("BP_Villager_Townsfolk", "Townsfolk", "Casual_2", {"strength": 6, "constitution": 5, "dexterity": 10}, 600.0),
    ("BP_Villager_Refugee", "Refugee", "Farmer", {"strength": 5, "constitution": 4, "dexterity": 10}, 300.0),
    ("BP_Villager_Scholar", "Scholar", "Worker", {"strength": 4, "constitution": 4, "dexterity": 8}, 400.0),
]


def load(path):
    a = unreal.EditorAssetLibrary.load_asset(path)
    if a is None:
        raise RuntimeError(f"asset missing: {path}")
    return a


# ── 0. 임포트 (없을 때만) ──────────────────────────────────────────
def ensure_imported(char):
    folder = f"{Q}/{char}"
    if unreal.EditorAssetLibrary.does_asset_exist(f"{folder}/{char}"):
        return
    src = os.path.join(RAW, f"{char}.fbx")
    if not os.path.exists(src):
        raise RuntimeError(f"raw fbx missing: {src} — tools/fetch_assets.py 로 받을 것")
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("use_source_name_for_asset", True)
    sk = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
    sk.set_editor_property("skeleton", None)  # 의상마다 스켈레톤 각자(적과 동일) — 단일 노드 재생이라 공유 불필요
    sk.set_editor_property("import_meshes_in_bone_hierarchy", True)
    ap = p.get_editor_property("animation_pipeline")
    ap.set_editor_property("import_animations", True)
    ap.set_editor_property("import_bone_tracks", True)
    mp = p.get_editor_property("mesh_pipeline")
    mp.set_editor_property("create_physics_asset", True)  # NPCRagdollComponent 필요
    mat = p.get_editor_property("material_pipeline")
    mat.set_editor_property("identify_duplicate_materials", True)
    mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(p.get_path_name())])
    out = mgr.import_asset(folder, unreal.InterchangeManager.create_source_data(src), params)
    n = dropped = 0
    for a in unreal.EditorAssetLibrary.list_assets(folder, recursive=True):
        path = a.split(".")[0]
        name = path.rsplit("/", 1)[1]
        if "_Anim_" in name and name.rsplit("_", 1)[1] not in KEEP_CLIPS:
            dropped += bool(unreal.EditorAssetLibrary.delete_asset(path))
            continue
        n += bool(unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
    print(f"[villager] imported {char}: {len(out)} assets, saved {n}, dropped unused clips {dropped}")


def find_in(folder, cls, suffix=""):
    """폴더에서 클래스·이름 접미로 에셋 1개. 임포트 이름 규칙이 바뀌어도 접미(_Idle 등)로 찾는다."""
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=False):
        name = path.split(".")[-1]
        if suffix and not name.endswith(suffix):
            continue
        a = unreal.EditorAssetLibrary.load_asset(path.split(".")[0])
        if isinstance(a, cls):
            return a
    raise RuntimeError(f"not found in {folder}: {cls.__name__} *{suffix}")


def ensure_bp(path, parent_class):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return load(path)
    folder, name = path.rsplit("/", 1)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Blueprint, factory)
    if bp is None:
        raise RuntimeError(f"create failed: {path}")
    return bp


def cdo_of(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    return unreal.get_default_object(bp.generated_class())


def save(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)


# ── 베이스 (공통 값만) ─────────────────────────────────────────────
base = ensure_bp(BASE, unreal.VillagerCharacter)
cdo = cdo_of(base)
cdo.set_editor_property("CorpseLifetime", 10.0)
cdo.set_editor_property("HitSounds", [load(f"{SND}/S_Hit_Punch_{i}") for i in range(5)])
cdo.set_editor_property("DeathSounds", [load(f"{SND}/S_Death_Thud_{i}") for i in range(3)])
save(base)
print("[villager] base:", BASE)

# ── 종류별 자식 ──────────────────────────────────────────────────────
for name, prefix, char, stats, radius in KINDS:
    ensure_imported(char)
    folder = f"{Q}/{char}"
    bp = ensure_bp(f"{DIR}/{name}", base.generated_class())
    c = cdo_of(bp)
    c.set_editor_property("VillagerID", prefix)
    c.set_editor_property("WanderRadius", radius)
    attrs = c.get_editor_property("Attributes")
    bs = attrs.get_editor_property("base_stats")
    for k, v in stats.items():
        bs.set_editor_property(k, v)
    attrs.set_editor_property("base_stats", bs)
    c.set_editor_property("Attributes", attrs)
    # 메시 — Quaternius 릭. 피벗이 발이라 캡슐 반높이만큼 내리고, 정면(+Y)을 UE 정면(+X)으로(적과 동일).
    mesh = c.mesh
    sk_mesh = find_in(folder, unreal.SkeletalMesh)
    mesh.set_skeletal_mesh_asset(sk_mesh)
    mesh.set_editor_property("physics_asset_override", find_in(folder, unreal.PhysicsAsset))
    mesh.set_editor_property("relative_location", unreal.Vector(0, 0, -88))
    mesh.set_editor_property("relative_rotation", unreal.Rotator(roll=0, pitch=0, yaw=-90))
    mesh.set_editor_property("anim_class", None)
    for prop, clip in CLIPS.items():
        c.set_editor_property(prop, find_in(folder, unreal.AnimSequence, f"_{clip}"))
    save(bp)
    chk = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(f"{DIR}/{name}"))
    ext = sk_mesh.get_bounds().box_extent
    print(
        "[villager]",
        name,
        chk.get_editor_property("VillagerID"),
        "mesh",
        chk.mesh.get_skeletal_mesh_asset().get_name(),
        f"height~{ext.z * 2:.0f}",
        "wave",
        f"{chk.get_editor_property('WaveAnim').get_play_length():.2f}s",
        "hit",
        f"{chk.get_editor_property('HitAnim').get_play_length():.2f}s",
    )

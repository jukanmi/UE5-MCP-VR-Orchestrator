"""서브퀘스트 적 BP 생성 — /Game/Blueprint/Enemy/{BP_Enemy, BP_Bandit, BP_OrcVagron, BP_KnightWraith}. 멱등(있으면 값만 갱신).

에디터 안에서 실행: MCP `ue_run_python(mode="file", script="<이 파일 절대경로>")`.
BP_Enemy(부모 C++ AEnemyCharacter)에 SmartNPC 와 같은 메시·ABP·Physics Asset·AM_Attack 을 박고,
종류별 자식 BP 가 EnemyID·BaseStats·스케일만 다르게 가진다. 스탯 → 파생치는 BeginPlay 의 RecalculateCombatStats.
"""

import unreal

DIR = "/Game/Blueprint/Enemy"
BASE = f"{DIR}/BP_Enemy"
SKEL = "/Game/Core/Mesh/NPC/X_Bot"
ABP = "/Game/Blueprint/NPC/ABP_SmartNPC"
PA = "/Game/Blueprint/NPC/PA_SmartNPC"
AM_ATTACK = "/Game/Core/Animation/interact/AM_Attack"

# 종류: (BP 이름, EnemyID, BaseStats 덮어쓰기, 스케일, 공격 쿨다운)
# 참고 공식(CharacterAttributes.h): ATK=Str×1.5, DEF=Con, HP=150+Con×15, 속도는 Dex.
# 플레이어 스윙 데미지는 ½mv² 클램프라 HP 가 곧 필요 타수 — 도적 3~4방, 오크 10방+ 목표.
KINDS = [
    ("BP_Bandit", "Bandit_Raider", {"strength": 12, "constitution": 6, "dexterity": 14}, 1.0, 1.4),
    ("BP_OrcVagron", "Orc_Vagron", {"strength": 24, "constitution": 30, "dexterity": 8}, 1.35, 2.2),
    ("BP_KnightWraith", "Knight_Wraith", {"strength": 14, "constitution": 10, "dexterity": 20}, 1.05, 1.2),
]


def load(path):
    a = unreal.EditorAssetLibrary.load_asset(path)
    if a is None:
        raise RuntimeError(f"asset missing: {path}")
    return a


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


# ── 베이스 ───────────────────────────────────────────────────────────
base = ensure_bp(BASE, unreal.EnemyCharacter)
cdo = cdo_of(base)
mesh = cdo.mesh
mesh.set_skeletal_mesh_asset(load(SKEL))
mesh.set_anim_instance_class(load(ABP).generated_class())
mesh.set_editor_property("physics_asset_override", load(PA))
mesh.set_editor_property("relative_location", unreal.Vector(0, 0, -90))
mesh.set_editor_property("relative_rotation", unreal.Rotator(roll=0, pitch=0, yaw=-90))
cdo.set_editor_property("AttackMontage", load(AM_ATTACK))
cdo.set_editor_property("AttackCooldown", 1.5)
cdo.set_editor_property("CorpseLifetime", 5.0)
save(base)
print("[enemy] base:", BASE)

# ── 종류별 자식 ──────────────────────────────────────────────────────
for name, enemy_id, stats, scale, cooldown in KINDS:
    bp = ensure_bp(f"{DIR}/{name}", base.generated_class())
    c = cdo_of(bp)
    c.set_editor_property("EnemyID", enemy_id)
    attrs = c.get_editor_property("Attributes")
    bs = attrs.get_editor_property("base_stats")
    for k, v in stats.items():
        bs.set_editor_property(k, v)
    attrs.set_editor_property("base_stats", bs)
    c.set_editor_property("Attributes", attrs)
    c.set_editor_property("AttackCooldown", cooldown)
    c.capsule_component.set_editor_property("relative_scale3d", unreal.Vector(scale, scale, scale))
    save(bp)
    # 검증 — 저장된 CDO 재조회
    chk = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(f"{DIR}/{name}"))
    print(
        "[enemy]",
        name,
        chk.get_editor_property("EnemyID"),
        "str",
        chk.get_editor_property("Attributes").get_editor_property("base_stats").get_editor_property("strength"),
        "mesh",
        chk.mesh.get_skeletal_mesh_asset() is not None,
        "montage",
        chk.get_editor_property("AttackMontage") is not None,
    )

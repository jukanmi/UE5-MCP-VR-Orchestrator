"""서브퀘스트 적 BP 생성 — /Game/Blueprint/Enemy/{BP_Enemy, BP_Bandit, BP_OrcVagron, BP_KnightWraith}. 멱등(있으면 값만 갱신).

에디터 안에서 실행: MCP `ue_run_python(mode="file", script="<이 파일 절대경로>")`.
메시·애니는 Quaternius RPG Characters(CC0, RawAssets → /Game/Enemy/Quaternius, tools/fetch_assets.py + FBX 임포트).
AnimBP 없음 — AEnemyCharacter 가 Idle/Walk/Run/Attack 클립을 단일 노드로 직접 재생하므로 BP 엔 클립 4개만 꽂는다.
종류별 자식 BP 가 EnemyID·BaseStats·스케일·메시·클립을 다르게 가진다. 스탯 → 파생치는 BeginPlay 의 RecalculateCombatStats.
"""

import unreal

DIR = "/Game/Blueprint/Enemy"
BASE = f"{DIR}/BP_Enemy"
Q = "/Game/Enemy/Quaternius"

# 종류: (BP 이름, EnemyID, Quaternius 캐릭터, 공격 클립, 타격 시점(초), BaseStats, 스케일, 쿨다운, 도주 HP 비율)
# 참고 공식(CharacterAttributes.h): ATK=Str×1.5, DEF=Con, HP=150+Con×15, 속도는 Dex.
# 플레이어 스윙 데미지는 ½mv² 클램프(최대 100)라 HP 가 곧 필요 타수 — 도적 3~4방, 오크 10방+ 목표.
KINDS = [
    (
        "BP_Bandit",
        "Bandit_Raider",
        "Rogue",
        "Dagger_Attack",
        0.35,
        {"strength": 12, "constitution": 6, "dexterity": 14},
        1.0,
        1.4,
        0.25,
    ),
    (
        "BP_OrcVagron",
        "Orc_Vagron",
        "Warrior",
        "Sword_Attack",
        0.5,
        {"strength": 24, "constitution": 30, "dexterity": 8},
        1.35,
        2.2,
        0.0,
    ),
    (
        "BP_KnightWraith",
        "Knight_Wraith",
        "Wizard",
        "Staff_Attack",
        0.45,
        {"strength": 14, "constitution": 10, "dexterity": 20},
        1.05,
        1.2,
        0.15,
    ),
]


def load(path):
    a = unreal.EditorAssetLibrary.load_asset(path)
    if a is None:
        raise RuntimeError(f"asset missing: {path}")
    return a


def anim(char, clip):
    return load(f"{Q}/{char}/{char}_Anim_CharacterArmature_{clip}")


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
base = ensure_bp(BASE, unreal.EnemyCharacter)
cdo = cdo_of(base)
cdo.set_editor_property("CorpseLifetime", 5.0)
save(base)
print("[enemy] base:", BASE)

# ── 종류별 자식 ──────────────────────────────────────────────────────
for name, enemy_id, char, attack_clip, hit_delay, stats, scale, cooldown, flee in KINDS:
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
    c.set_editor_property("AttackHitDelay", hit_delay)
    c.set_editor_property("FleeHealthPct", flee)
    # 메시 — Quaternius 릭(X_Bot 아님). 피벗이 발이라 캡슐 반높이만큼 내리고, FBX 정면(+Y)을 UE 정면(+X)으로.
    mesh = c.mesh
    mesh.set_skeletal_mesh_asset(load(f"{Q}/{char}/{char}"))
    mesh.set_editor_property("physics_asset_override", load(f"{Q}/{char}/{char}_PhysicsAsset"))
    mesh.set_editor_property("relative_location", unreal.Vector(0, 0, -88))
    mesh.set_editor_property("relative_rotation", unreal.Rotator(roll=0, pitch=0, yaw=-90))
    mesh.set_editor_property("anim_class", None)
    c.set_editor_property("IdleAnim", anim(char, "Idle"))
    c.set_editor_property("WalkAnim", anim(char, "Walk"))
    c.set_editor_property("RunAnim", anim(char, "Run"))
    c.set_editor_property("AttackAnim", anim(char, attack_clip))
    c.capsule_component.set_editor_property("relative_scale3d", unreal.Vector(scale, scale, scale))
    save(bp)
    chk = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(f"{DIR}/{name}"))
    print(
        "[enemy]",
        name,
        chk.get_editor_property("EnemyID"),
        "mesh",
        chk.mesh.get_skeletal_mesh_asset().get_name(),
        "attack",
        chk.get_editor_property("AttackAnim").get_name(),
        f"len={chk.get_editor_property('AttackAnim').get_play_length():.2f}s",
    )

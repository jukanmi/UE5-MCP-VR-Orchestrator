"""서브퀘스트 적 BP 생성 — /Game/Blueprint/Enemy/{BP_Enemy, BP_Enemy_Bandit, BP_Enemy_OrcVagron, BP_Enemy_Wraith}. 멱등(있으면 값만 갱신).

에디터 안에서 실행: MCP `ue_run_python(mode="file", script="<이 파일 절대경로>")`.
메시·애니는 Quaternius(RPG Characters CC0 · Bestiary/UAL itch.io → tools/import_itch_assets.py), 사운드는 Kenney(CC0).
AnimBP 없음 — AEnemyCharacter 가 Idle/Walk/Run/Attack 클립을 단일 노드로 직접 재생하므로 BP 엔 클립 4개만 꽂는다.
종류별 자식 BP 가 EnemyID·BaseStats·스케일·메시·클립·손 소품을 다르게 가진다. 스탯 → 파생치는 BeginPlay 의 RecalculateCombatStats.
클립 이름 규칙은 릭마다 다르다 — RPG Characters 는 {char}_Anim_CharacterArmature_{clip}, Bestiary 는 UAL 리타겟 결과 {char}_{clip}.
"""

import unreal

DIR = "/Game/Blueprint/Enemy"
BASE = f"{DIR}/BP_Enemy"
Q = "/Game/Enemy/Quaternius"
PROPS = "/Game/Core/Mesh/Props"

# 릭별 클립 이름 — RPG Characters(Rogue/Warrior/Wizard) vs Bestiary(Imp/Puglin, UAL 리타겟).
RPG = {"idle": "Idle", "walk": "Walk", "run": "Run", "fmt": "{char}_Anim_CharacterArmature_{clip}"}
UAL = {"idle": "Idle_Loop", "walk": "Walk_Loop", "run": "Jog_Fwd_Loop", "fmt": "{char}_{clip}"}

# 종류: (BP 이름, EnemyID, Quaternius 캐릭터, 릭, 공격 클립, 타격 시점(초), BaseStats, 스케일, 쿨다운, 도주 HP 비율, 손 소품, 드랍 아이템 ID)
# 드랍: ItemRegistry ItemID 또는 None. 도적 → BanditInsignia(수집 서브퀘스트 s_hunt_forest_raiders), 망령 → MagicGem, 오크 → TreasureKey.
# 손 소품: (Props 메시 이름, 본/소켓, 상대 위치, 상대 회전) 또는 None. 오프셋은 T 포즈 SceneCapture 로 맞춘 값 — PIE 에서 재확인.
# 참고 공식(CharacterAttributes.h): ATK=Str×1.5, DEF=Con, HP=150+Con×15, 속도는 Dex.
# 플레이어 스윙 데미지는 ½mv² 클램프(최대 100)라 HP 가 곧 필요 타수 — 도적 3~4방, 오크 10방+ 목표.
# 스케일(VR 체감 최적화):
#   - Quaternius 원본 1.0은 키 206cm 거인이었음. 주민(155cm) 대비 체형 정렬:
#   - Bandit (Rogue): 0.80 (키 ~165cm, 일반 인간 남성 도적 체구)
#   - OrcVagron (Warrior): 1.05 (키 ~208cm, 떡대 있고 위압감 있는 오크 전사)
#   - Wraith (Wizard): 0.85 (키 ~178cm, 망령 마법사 실루엣)
#   - Imp: 0.70 (키 ~110cm, 소형 날렵 악마)
#   - Puglin: 0.70 (키 ~125cm, 소형 그런트)
KINDS = [
    (
        "BP_Enemy_Bandit",
        "Bandit_Raider",
        "Rogue",
        RPG,
        "Dagger_Attack",
        0.35,
        {"strength": 12, "constitution": 6, "dexterity": 14},
        0.80,
        1.4,
        0.25,
        None,
        "BanditInsignia",
    ),
    (
        "BP_Enemy_OrcVagron",
        "Orc_Vagron",
        "Warrior",
        RPG,
        "Sword_Attack",
        0.5,
        {"strength": 24, "constitution": 30, "dexterity": 8},
        1.05,
        2.2,
        0.0,
        ("Torch_Metal", "Fist_L", unreal.Vector(0, 0, 0), unreal.Rotator(roll=0, pitch=0, yaw=0)),
        "TreasureKey",
    ),
    (
        "BP_Enemy_Wraith",
        "Knight_Wraith",
        "Wizard",
        RPG,
        "Staff_Attack",
        0.45,
        {"strength": 14, "constitution": 10, "dexterity": 20},
        0.85,
        1.2,
        0.15,
        None,
        "MagicGem",
    ),
    # Bestiary 무료판 2종(itch.io, UAL 리타겟). 둘 다 맨손 — Punch_Cross 는 타격 프레임이 빠르다(0.3s).
    (
        "BP_Enemy_Imp",
        "Imp_Fiend",
        "Imp",
        UAL,
        "Punch_Cross",
        0.3,
        {"strength": 10, "constitution": 5, "dexterity": 18},  # 빠르고 약함 — 2~3방
        0.70,
        1.0,
        0.3,
        None,
        None,
    ),
    (
        "BP_Enemy_Puglin",
        "Puglin_Grunt",
        "Puglin",
        UAL,
        "Punch_Cross",
        0.3,
        {"strength": 14, "constitution": 12, "dexterity": 10},  # 작고 질김 — 4~5방
        0.70,
        1.6,
        0.2,
        None,
        None,
    ),
]


def load(path):
    a = unreal.EditorAssetLibrary.load_asset(path)
    if a is None:
        raise RuntimeError(f"asset missing: {path}")
    return a


def anim(char, rig, clip):
    return load(f"{Q}/{char}/" + rig["fmt"].format(char=char, clip=clip))


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
SND = "/Game/Enemy/Sounds"


def sounds(prefix, n):
    return [load(f"{SND}/S_{prefix}_{i}") for i in range(n)]


base = ensure_bp(BASE, unreal.EnemyCharacter)
cdo = cdo_of(base)
cdo.set_editor_property("CorpseLifetime", 5.0)
# 사운드(Kenney CC0, tools/fetch_assets.py → 에디터 임포트). 자식이 상속 — 종류별 차이 없음.
cdo.set_editor_property("HitSounds", sounds("Hit_Punch", 5))
cdo.set_editor_property("AttackSounds", [load(f"{SND}/S_Swing_Slice_{i}") for i in (1, 2, 3)])
cdo.set_editor_property("DeathSounds", sounds("Death_Thud", 3))
save(base)
print("[enemy] base:", BASE)

# 레벨 스포너 및 과거 명칭 호환 매핑
ALIASES = {
    "BP_Enemy_Bandit": "BP_Bandit",
    "BP_Enemy_OrcVagron": "BP_OrcVagron",
    "BP_Enemy_Wraith": "BP_KnightWraith",
}

# ── 종류별 자식 ──────────────────────────────────────────────────────
for name, enemy_id, char, rig, attack_clip, hit_delay, stats, scale, cooldown, flee, prop, drop in KINDS:
    target_names = [name]
    if name in ALIASES:
        target_names.append(ALIASES[name])

    for target_bp_name in target_names:
        bp = ensure_bp(f"{DIR}/{target_bp_name}", base.generated_class())
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
        c.set_editor_property("DropItemID", drop or "")  # 사망 드랍(ItemRegistry ID). None = 드랍 없음
        # 메시 — Quaternius 릭(X_Bot 아님). 피벗이 발이라 캡슐 반높이만큼 내리고, 정면(+Y, RPG·Bestiary 공통)을 UE 정면(+X)으로.
        mesh = c.mesh
        mesh.set_skeletal_mesh_asset(load(f"{Q}/{char}/{char}"))
        mesh.set_editor_property("physics_asset_override", load(f"{Q}/{char}/{char}_PhysicsAsset"))
        mesh.set_editor_property("relative_location", unreal.Vector(0, 0, -88))
        mesh.set_editor_property("relative_rotation", unreal.Rotator(roll=0, pitch=0, yaw=-90))
        mesh.set_editor_property("anim_class", None)
        c.set_editor_property("IdleAnim", anim(char, rig, rig["idle"]))
        c.set_editor_property("WalkAnim", anim(char, rig, rig["walk"]))
        c.set_editor_property("RunAnim", anim(char, rig, rig["run"]))
        c.set_editor_property("AttackAnim", anim(char, rig, attack_clip))
        c.capsule_component.set_editor_property("relative_scale3d", unreal.Vector(scale, scale, scale))
        # 손 소품 — 소켓 부착은 AEnemyCharacter::PostInitializeComponents(HandPropSocket). 여기선 메시·손안 오프셋만.
        hp = c.get_editor_property("HandProp")
        hp.set_static_mesh(load(f"{PROPS}/{prop[0]}") if prop else None)
        c.set_editor_property("HandPropSocket", prop[1] if prop else "hand_l")
        hp.set_editor_property("relative_location", prop[2] if prop else unreal.Vector(0, 0, 0))
        hp.set_editor_property("relative_rotation", prop[3] if prop else unreal.Rotator(roll=0, pitch=0, yaw=0))
        save(bp)
        chk = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(f"{DIR}/{target_bp_name}"))
        print(
            "[enemy]",
            target_bp_name,
            chk.get_editor_property("EnemyID"),
            "scale",
            scale,
            "drop",
            chk.get_editor_property("DropItemID"),
            "mesh",
            chk.mesh.get_skeletal_mesh_asset().get_name(),
            "attack",
            chk.get_editor_property("AttackAnim").get_name(),
            f"len={chk.get_editor_property('AttackAnim').get_play_length():.2f}s",
            "prop",
            getattr(chk.get_editor_property("HandProp").static_mesh, "get_name", lambda: None)(),
            "@",
            chk.get_editor_property("HandPropSocket"),
        )


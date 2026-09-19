"""주민 BP 생성 — /Game/Blueprint/Villager/{BP_Villager, BP_Villager_Townsfolk, BP_Villager_Refugee, BP_Villager_Scholar, BP_Villager_Merchant}. 멱등(있으면 값만 갱신).

에디터 안에서 실행: MCP `ue_run_python(mode="file", script="<이 파일 절대경로>")`.
메시·애니는 Quaternius Ultimate Modular Characters(CC0) "Individual Characters" FBX — 의상 1개 = 스켈레탈 메시 1 + 클립 24 내장,
릭은 적(RPG Characters)과 같은 CharacterArmature 계열이라 클립 이름도 같은 규칙({char}_Anim_CharacterArmature_{clip}).
없으면 여기서 Interchange 로 /Game/Villager/Quaternius/{char} 에 임포트한다(텍스처 없음 — 단색 머티리얼).
AnimBP 없음 — AVillagerCharacter 가 Idle/Walk/Run/Wave/HitRecieve 를 단일 노드로 직접 재생하므로 BP 엔 클립 5개만 꽂는다.
종류별 자식 BP 가 VillagerID 접두·BaseStats·의상·배회 반경을 다르게 가진다. 개체 번호(Townsfolk_3)는 build_story_scene.py 가 붙인다.
대사 풀·키워드 규칙·비트별 길 안내(LINES/RULES/DIRECTIONS)와 상인 재고(Phase 3)도 여기에 데이터로 둔다 — 대사 데이터의 단일 원본.
AVillagerCharacter 가 로컬 규칙으로 응답(서버·LLM 0): 길 안내 키워드 → 종류 키워드 규칙 → 기본 풀.
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
    ("BP_Villager_Merchant", "Merchant", "Casual_Hoodie", {"strength": 6, "constitution": 5, "dexterity": 8}, 0.0),
]

# 상인 재고(Inventory.InitialDefaultItems) — ItemID 1개 = 1개. 소모품만(장비는 시작 골드 150 으로 못 산다). 총 18개.
# 가격은 DT_ItemRegistry BaseValue(HealthPotion 60·Bandage 12·Bread 8·Torch 15·WaterSkin 15), 매입가 = 반값(최소 1).
STOCK = {
    "Merchant": ["HealthPotion"] * 3 + ["Bandage"] * 5 + ["Bread"] * 5 + ["Torch"] * 2 + ["WaterSkin"] * 3,
}

# ── 대사 (전 종류 공통: 베이스 BP) ─────────────────────────────────
# 길 안내 분기 키워드 — 플레이어 채팅에 하나라도 포함되면 현재 스토리 비트의 안내문(DIRECTIONS)으로 응답.
DIRECTION_KEYWORDS = ["길", "어디", "퀘스트", "가야", "어떻게 가", "목적지"]
# 비트 id(main.yaml) → 구역 이름+방향. 주민은 "방향", quest_log 는 "목적" — 역할 분리.
DIRECTIONS = {
    "b1_kingdom_fall": "성문 안쪽에 경비병이 서 있네. 그 사람부터 만나 보게.",
    "b2_james_order": "광장으로 가 보게. James 가 자네를 찾고 있었어.",
    "b3_recruit_party": "서쪽 도서관에 Moca 가 있고, 숲 빈터엔 Skadi 가 있다더군. 둘 다 힘이 될 걸세.",
    "b4_reforge_and_elara": "동쪽 다리를 건너면 전초기지야. 조심하게, 마물이 많아.",
    "b5_moca_betrayal": "광장의 James 한테 가 보게. 급한 얼굴이던데.",
    "b6_save_moca_and_boss": "북동쪽 마왕성… 정말 갈 텐가? 무운을 비네.",
    "b7_epilogue": "성문 쪽으로 가 보게. 다들 자네를 기다리고 있어.",
    "end": "이제 다 끝났지. 편히 쉬게.",
}
# 활성 서브퀘스트 id → 덧붙이는 한 줄.
SIDE_DIRECTIONS = {
    "s_moca_herbs": "약초는 남쪽 숲 빈터에 자라네.",
}
# 스토리 비트 미수신(서버 응답 전)·표에 없는 비트.
NO_STORY = "글쎄, 성문 쪽 경비병한테 물어보게."

# ── 종류별 기본 풀(Interact 인사·매칭 없음) + 키워드 규칙([키워드들], [응답들]) ──
LINES = {
    "Townsfolk": [
        "안녕하신가. 오늘도 장이 섰어.",
        "요즘 밤엔 문단속 잘 하게. 흉흉해.",
        "낯선 얼굴이군. 마을에 온 걸 환영하네.",
        "빵집 냄새 좋지? 아침마다 저래.",
    ],
    "Refugee": [
        "…고향은 불탔어. 여기까지 오는 데 사흘 걸렸지.",
        "먹을 게 좀 없나. 아이들이 굶고 있어.",
        "마물이 또 올까 무서워. 자네는 싸울 줄 아나?",
    ],
    "Scholar": [
        "쉿, 도서관에선 조용히. 뭘 찾나?",
        "옛 기록에 따르면 마왕은 한 번 봉인된 적이 있네.",
        "책은 제자리에 꽂아 두게. 부탁이야.",
    ],
    "Merchant": [
        "어서 오게. 탁자 위 물건은 집으면 바로 자네 거야 — 값만 치르면.",
        "팔 게 있으면 옆 상자에 넣게. 반값이지만 현금일세.",
        "치유 물약은 60 골드. 싸게 파는 거야, 요즘 약초값이 올라서.",
        "이름표에 값이 적혀 있네. 흥정은 안 받아.",
    ],
}
RULES = {
    "Townsfolk": [
        (["안녕", "반가", "인사"], ["그래, 반갑네.", "안녕하신가."]),
        (["마왕", "마물", "괴물"], ["북동쪽 성에 마왕이 산다지. 밤엔 그쪽 얼씬도 마.", "마물 얘기는 그만. 소름 돋아."]),
        (["상인", "물건", "사고", "팔"], ["시장 가판대 상인이 이것저것 팔아. 값은 좀 세지만.", "물건은 시장에서. 광장 옆이야."]),
        (["경비", "병사"], ["경비병은 성문 안쪽에 있네.", "경비병 말은 잘 듣게. 그 사람 눈이 매서워."]),
    ],
    "Refugee": [
        (["안녕", "반가"], ["…안녕. 살아 있으니 됐지.", "안녕하시오."]),
        (["고향", "마을", "왔"], ["동쪽 마을에서 왔어. 지금은 잿더미야.", "우린 강 건너에서 도망쳐 왔네."]),
        (["마왕", "마물", "괴물"], ["그놈들이 우리 마을을 태웠어. 제발… 막아 주게.", "마물이 오면 은신처로. 그것밖에 못 해."]),
        (["음식", "먹", "배고"], ["빵 한 조각이라도 있으면… 고맙겠네.", "시장 상인이 빵을 판다던데 돈이 없어."]),
    ],
    "Scholar": [
        (["안녕", "반가"], ["아, 안녕하시오. 조용히 부탁해요.", "반갑소. 책 보러 왔소?"]),
        (["마왕", "봉인", "역사", "기록"], ["마왕은 성검으로 봉인됐다고 기록돼 있소. 그 검이 어디 있는지는… 흠.", "기록실 안쪽 서가를 보시오. 마왕 봉인 연대기가 있소."]),
        (["Moca", "모카", "마법"], ["Moca 는 안쪽 열람실에 자주 있소. 마법서 쪽이오.", "마법에 관해선 Moca 가 나보다 낫소."]),
        (["책", "도서", "읽"], ["책은 대출 안 되오. 여기서 읽으시오.", "찾는 책이 있으면 서가 번호를 말해 보시오."]),
    ],
    "Merchant": [
        (["안녕", "반가"], ["어서 오게. 뭐 찾나?", "반갑네. 구경은 공짜야."]),
        (["가격", "얼마", "값", "비싸"], ["이름표에 적힌 대로야. 물약 60, 붕대 12, 빵 8.", "비싸긴, 마물 때문에 길이 끊겨 원가가 올랐어."]),
        (["팔", "매입", "사줘", "사 줘"], ["옆 상자에 넣게. 반값에 쳐주지.", "퀘스트 물건은 안 사네. 값이 없어."]),
        (["물약", "포션", "치유"], ["치유 물약은 탁자 위에. 60 골드일세.", "물약은 세 병뿐이야. 서두르게."]),
        (["빵", "음식", "먹"], ["빵은 8 골드. 피난민한테 나눠 주면 좋아하지.", "호밀빵이야. 딱딱하지만 든든하네."]),
    ],
}


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
cdo.set_editor_property("DirectionKeywords", DIRECTION_KEYWORDS)
cdo.set_editor_property("BeatDirections", DIRECTIONS)
cdo.set_editor_property("SideDirections", SIDE_DIRECTIONS)
cdo.set_editor_property("NoStoryDirection", NO_STORY)
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
    c.set_editor_property("DefaultLines", LINES[prefix])
    rules = []
    for keywords, lines in RULES[prefix]:
        r = unreal.VillagerKeywordRule()
        r.set_editor_property("keywords", keywords)
        r.set_editor_property("lines", lines)
        rules.append(r)
    c.set_editor_property("KeywordRules", rules)
    # 재고 — 상인만. 다른 종류는 빈 목록(멱등: 이전 값 제거).
    c.get_editor_property("Inventory").set_editor_property("InitialDefaultItems", STOCK.get(prefix, []))
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
        "lines",
        len(chk.get_editor_property("DefaultLines")),
        "rules",
        len(chk.get_editor_property("KeywordRules")),
        "beats",
        len(chk.get_editor_property("BeatDirections")),
        "stock",
        len(chk.get_editor_property("Inventory").get_editor_property("InitialDefaultItems")),
        "mesh",
        chk.mesh.get_skeletal_mesh_asset().get_name(),
        f"height~{ext.z * 2:.0f}",
        "wave",
        f"{chk.get_editor_property('WaveAnim').get_play_length():.2f}s",
        "hit",
        f"{chk.get_editor_property('HitAnim').get_play_length():.2f}s",
    )

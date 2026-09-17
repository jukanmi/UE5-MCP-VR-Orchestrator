"""스토리 씬 7구역 빌드 — Sample 레벨 (2026-09-18). 멱등: 라벨 SCN_* 액터 전부 삭제 후 재생성.

에디터 안에서 실행: MCP `ue_run_python(mode="file", script="<이 파일 절대경로>")` 또는 에디터 Python 콘솔
`py "C:/github/UE5_MCP_VR/tools/build_story_scene.py"`. 전제: Content/StarterContent (Engine/Samples 에서 복사, Memo 참조),
C++ AStoryZoneTrigger 빌드됨. 마지막에 save_current_level().

구역(ZoneName) · 중심: gate(-1030,400) plaza(-1030,-490) hideout(-2000,-1100) library(-4500,-2500)
forest(2500,-3500) outpost(7000,-500) citadel(14000,7500). NPC 는 각자 스토리 위치로 이동, PlayerStart 는 성문 밖 북쪽.
손으로 옮긴 소품은 재실행 시 초기화된다 — 좌표를 여기 고쳐라."""

import math
import random

import unreal

random.seed(7)

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

SC = "/Game/StarterContent"
MESH = {
    "wall": f"{SC}/Architecture/Wall_400x400",
    "wall3": f"{SC}/Architecture/Wall_400x300",
    "wall5": f"{SC}/Architecture/Wall_500x500",
    "door": f"{SC}/Architecture/Wall_Door_400x400",
    "floor": f"{SC}/Architecture/Floor_400x400",
    "pillar": f"{SC}/Architecture/Pillar_50x500",
    "rock": f"{SC}/Props/SM_Rock",
    "bush": f"{SC}/Props/SM_Bush",
    "chair": f"{SC}/Props/SM_Chair",
    "table": f"{SC}/Props/SM_TableRound",
    "shelf": f"{SC}/Props/SM_Shelf",
    "statue": f"{SC}/Props/SM_Statue",
    "lamp": f"{SC}/Props/SM_Lamp_Wall",
    "cone": f"{SC}/Shapes/Shape_Cone",
    "cyl": f"{SC}/Shapes/Shape_Cylinder",
    "cube": f"{SC}/Shapes/Shape_Cube",
    "pyramid": f"{SC}/Shapes/Shape_QuadPyramid",
    "torus": f"{SC}/Shapes/Shape_Torus",
}
MAT = {
    "stone": f"{SC}/Materials/M_CobbleStone_Rough",
    "brick": f"{SC}/Materials/M_Brick_Clay_Old",
    "wall": f"{SC}/Materials/M_Basic_Wall",
    "basalt": f"{SC}/Materials/M_Rock_Basalt",
    "wood": f"{SC}/Materials/M_Wood_Oak",
    "grass": f"{SC}/Materials/M_Ground_Grass",
    "rust": f"{SC}/Materials/M_Metal_Rust",
    "gold": f"{SC}/Materials/M_Metal_Gold",
}
FIRE = f"{SC}/Particles/P_Fire"

_asset_cache = {}


def load(path):
    if path not in _asset_cache:
        a = unreal.EditorAssetLibrary.load_asset(path)
        if a is None:
            raise RuntimeError(f"asset missing: {path}")
        _asset_cache[path] = a
    return _asset_cache[path]


def ground_z(x, y):
    hit = unreal.SystemLibrary.line_trace_single(
        WORLD,
        unreal.Vector(x, y, 5000),
        unreal.Vector(x, y, -5000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
        False,
        [],
        unreal.DrawDebugTrace.NONE,
        True,
    ).to_tuple()
    if hit[0] and hit[9] and "Landscape" in hit[9].get_class().get_name():
        return hit[5].z
    return 0.0


COUNTS = {}


def _label(zone):
    COUNTS[zone] = COUNTS.get(zone, 0) + 1
    return f"SCN_{zone}_{COUNTS[zone]:03d}"


def place(zone, mesh_key, x, y, yaw=0.0, scale=(1, 1, 1), mat=None, z_off=0.0, pitch=0.0, roll=0.0, z_abs=None):
    z = (ground_z(x, y) if z_abs is None else z_abs) + z_off
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z), unreal.Rotator(pitch, yaw, roll))
    a.set_mobility(unreal.ComponentMobility.STATIC)
    smc = a.static_mesh_component
    smc.set_static_mesh(load(MESH[mesh_key]))
    if mat:
        smc.set_material(0, load(MAT[mat]))
    a.set_actor_scale3d(unreal.Vector(*scale))
    a.set_actor_label(_label(zone))
    a.set_editor_property("is_spatially_loaded", False)
    return a


def fire(zone, x, y, z_off=120.0, scale=1.0):
    z = ground_z(x, y) + z_off
    a = EAS.spawn_actor_from_class(unreal.Emitter, unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
    a.set_template(load(FIRE))
    a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    a.set_actor_label(_label(zone))
    a.set_editor_property("is_spatially_loaded", False)
    # 받침 기둥
    place(zone, "cyl", x, y, scale=(0.3, 0.3, 1.1), mat="rust")
    return a


def trigger(zone, x, y, ext=(450, 450, 200)):
    z = ground_z(x, y) + 100
    a = EAS.spawn_actor_from_class(unreal.StoryZoneTrigger, unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
    a.set_editor_property("ZoneName", zone)
    a.get_editor_property("Box").set_box_extent(unreal.Vector(*ext))
    a.set_actor_label(f"SCN_{zone}_trigger")
    a.set_editor_property("is_spatially_loaded", False)
    return a


def wall_line(zone, x0, y0, x1, y1, seg=400.0, mesh="wall", mat="stone", door_at=None):
    """(x0,y0)→(x1,y1) 직선에 벽 세그먼트. door_at = 세그먼트 인덱스 (문으로 교체)."""
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    n = max(1, int(round(L / seg)))
    yaw = math.degrees(math.atan2(dy, dx))
    for i in range(n):
        t = i / n
        x, y = x0 + dx * t, y0 + dy * t
        key = "door" if door_at is not None and i == door_at else mesh
        place(zone, key, x, y, yaw=yaw, mat=mat)


def ring(zone, cx, cy, r, n, mesh="pillar", mat="stone", scale=(1, 1, 1), tilt=0.0):
    for i in range(n):
        ang = 2 * math.pi * i / n
        x, y = cx + r * math.cos(ang), cy + r * math.sin(ang)
        place(
            zone,
            mesh,
            x,
            y,
            yaw=math.degrees(ang),
            scale=scale,
            mat=mat,
            roll=random.uniform(-tilt, tilt),
            pitch=random.uniform(-tilt, tilt),
        )


def floor_grid(zone, cx, cy, nx, ny, mat="stone"):
    for i in range(nx):
        for j in range(ny):
            # Floor_400x400 피벗은 모서리(0..400) — 중심 정렬하려면 -200
            place(zone, "floor", cx + (i - (nx - 1) / 2) * 400 - 200, cy + (j - (ny - 1) / 2) * 400 - 200, mat=mat, z_off=2)


# ─────────────────────────────────────────────────────────────────────
def clear_scene():
    n = 0
    for a in EAS.get_all_level_actors():
        if a.get_actor_label().startswith("SCN_"):
            EAS.destroy_actor(a)
            n += 1
    print("[scene] cleared SCN_ actors:", n)


def find_actor(label=None, agent_id=None, cls=None):
    for a in EAS.get_all_level_actors():
        if label and a.get_actor_label() == label:
            return a
        if agent_id and a.get_class().get_name() == "BP_SmartNPC_C" and a.get_editor_property("AgentID") == agent_id:
            return a
        if cls and a.get_class().get_name() == cls:
            return a
    return None


def move_npc(agent_id, x, y, face_x=None, face_y=None):
    a = find_actor(agent_id=agent_id)
    if not a:
        print("[scene] NPC 없음:", agent_id)
        return
    z = ground_z(x, y) + 88
    loc = unreal.Vector(x, y, z)
    a.set_actor_location(loc, False, False)
    if face_x is not None:
        yaw = unreal.MathLibrary.find_look_at_rotation(loc, unreal.Vector(face_x, face_y, z)).yaw
        a.set_actor_rotation(unreal.Rotator(0, yaw, 0), False)
    print("[scene] NPC", agent_id, "→", loc)


# ─────────────────────────────────────────────────────────────────────
def build():
    V = (-1030.0, -490.0)  # 마을 기준점

    # ── gate: 성벽 y=+400, 문 중앙. PlayerStart 북쪽 밖 ──
    gx, gy = V[0], 400.0
    wall_line("gate", gx - 1400, gy, gx + 1400, gy, door_at=3)  # 7세그, 가운데(3) 문
    place("gate", "pillar", gx - 260, gy, mat="stone", scale=(1.6, 1.6, 1.2))
    place("gate", "pillar", gx + 260, gy, mat="stone", scale=(1.6, 1.6, 1.2))
    fire("gate", gx - 330, gy + 140)
    fire("gate", gx + 330, gy + 140)
    for dx, dy, s in [(-900, 300, 1.5), (1000, 250, 1.2), (-1500, 600, 2.0)]:
        place("gate", "rock", gx + dx, gy + dy, yaw=random.uniform(0, 360), scale=(s, s, s))
    trigger("gate", gx, gy + 100, ext=(700, 350, 200))
    ps = find_actor(cls="PlayerStart")
    if ps:
        z = ground_z(gx, gy + 1100) + 100
        ps.set_actor_location(unreal.Vector(gx, gy + 1100, z), False, False)
        ps.set_actor_rotation(unreal.Rotator(0, -90, 0), False)  # 남향(성문)
        print("[scene] PlayerStart →", ps.get_actor_location())
    move_npc("Guard", gx, gy - 250, gx, gy + 800)

    # ── plaza: 마을 중앙 바닥 + 제단 ──
    px, py = V
    floor_grid("plaza", px, py, 3, 3, mat="stone")
    place("plaza", "cube", px, py, scale=(2.0, 2.0, 0.3), mat="basalt", z_off=2)
    place("plaza", "statue", px, py, z_off=32, scale=(1.2, 1.2, 1.2), mat="gold")
    ring("plaza", px, py, 520, 4, mesh="pillar", mat="stone")
    place("plaza", "chair", px - 500, py + 300, yaw=90, mat="wood")
    place("plaza", "chair", px + 500, py + 300, yaw=-90, mat="wood")
    trigger("plaza", px, py, ext=(600, 600, 200))
    move_npc("James", px + 230, py + 190, px, py)

    # ── hideout: 작은 건물 ──
    hx, hy = px - 970, py - 610
    floor_grid("hideout", hx, hy, 2, 2, mat="wood")
    wall_line("hideout", hx - 400, hy - 400, hx + 400, hy - 400, mat="brick")
    wall_line("hideout", hx + 400, hy - 400, hx + 400, hy + 400, mat="brick")
    wall_line("hideout", hx + 400, hy + 400, hx - 400, hy + 400, mat="brick", door_at=1)  # 북면 문(마을 쪽)
    wall_line("hideout", hx - 400, hy + 400, hx - 400, hy - 400, mat="brick")
    place("hideout", "table", hx, hy - 100, mat="wood")
    place("hideout", "shelf", hx - 300, hy - 300, yaw=90, mat="wood")
    place("hideout", "lamp", hx + 360, hy, yaw=180, z_off=200)
    trigger("hideout", hx, hy, ext=(380, 380, 200))
    bed = find_actor(cls="BP_Bed_C")
    if bed:
        z = ground_z(hx + 200, hy + 150)
        bed.set_actor_location(unreal.Vector(hx + 200, hy + 150, z), False, False)
        bed.set_actor_rotation(unreal.Rotator(0, 90, 0), False)
        print("[scene] BP_Bed → hideout")

    # ── library: 폐허 ──
    lx, ly = -4500.0, -2500.0
    floor_grid("library", lx, ly, 3, 2, mat="stone")
    ring("library", lx, ly, 700, 8, mesh="pillar", mat="stone", tilt=12.0)
    place("library", "wall3", lx - 600, ly - 500, yaw=0, mat="brick", roll=8)
    place("library", "wall3", lx + 500, ly - 520, yaw=15, mat="brick", pitch=-6)
    place("library", "wall3", lx + 650, ly + 300, yaw=95, mat="brick", roll=-10)
    for i, (dx, dy, yaw) in enumerate([(-300, 380, 0), (100, 380, 0), (500, 380, 0), (-650, 0, 90)]):
        place("library", "shelf", lx + dx, ly + dy, yaw=yaw, mat="wood", roll=(-6 if i == 2 else 0))
    for dx, dy, s in [(-900, -900, 1.4), (900, 800, 1.1), (200, -950, 0.9)]:
        place("library", "rock", lx + dx, ly + dy, yaw=random.uniform(0, 360), scale=(s, s, s))
    place("library", "chair", lx + 80, ly + 60, yaw=180, mat="wood")
    place("library", "table", lx + 80, ly - 120, mat="wood")
    trigger("library", lx, ly, ext=(900, 800, 200))
    move_npc("Moca", lx + 80, ly + 170, lx + 80, ly - 120)

    # ── forest: 나무·덤불, HerbBasket, Skadi ──
    fx, fy = 2500.0, -3500.0
    for i in range(12):
        ang = random.uniform(0, 2 * math.pi)
        r = random.uniform(300, 1300)
        tx, ty = fx + r * math.cos(ang), fy + r * math.sin(ang)
        h = random.uniform(2.5, 4.0)
        place("forest", "cyl", tx, ty, scale=(0.45, 0.45, h), mat="wood", z_off=0)
        place("forest", "cone", tx, ty, scale=(2.6, 2.6, 3.2), mat="grass", z_off=h * 100 * 0.55)
    for i in range(10):
        ang = random.uniform(0, 2 * math.pi)
        r = random.uniform(200, 1500)
        place(
            "forest",
            "bush",
            fx + r * math.cos(ang),
            fy + r * math.sin(ang),
            yaw=random.uniform(0, 360),
            scale=(random.uniform(0.8, 1.4),) * 3,
        )
    for i in range(5):
        ang = random.uniform(0, 2 * math.pi)
        r = random.uniform(400, 1400)
        s = random.uniform(0.7, 1.6)
        place(
            "forest",
            "rock",
            fx + r * math.cos(ang),
            fy + r * math.sin(ang),
            yaw=random.uniform(0, 360),
            scale=(s, s, s),
        )
    trigger("forest", fx, fy, ext=(1200, 1200, 250))
    # HerbBasket 드롭
    drop_cls = unreal.EditorAssetLibrary.load_blueprint_class("/Game/Blueprint/Entity/BP_DropItem")
    dz = ground_z(fx + 100, fy + 100) + 20
    d = EAS.spawn_actor_from_class(drop_cls, unreal.Vector(fx + 100, fy + 100, dz), unreal.Rotator(0, 0, 0))
    item = d.get_editor_property("ItemData")
    item.set_editor_property("item_template_id", "HerbBasket")
    d.set_editor_property("ItemData", item)  # PostEditChange → ItemTemplateID 기준 메시 교체
    d.set_actor_label("SCN_forest_HerbBasket")
    d.set_editor_property("is_spatially_loaded", False)
    print("[scene] HerbBasket drop →", d.get_actor_location())
    # 보급로 바위 + Skadi
    place("forest", "rock", 3500, -2500, scale=(2.2, 2.2, 1.2))
    move_npc("Skadi", 3500, -2200, 7000, -500)

    # ── outpost: 울타리 사각 + 횃불 + 포로 우리 ──
    ox, oy = 7000.0, -500.0
    R = 1000
    wall_line("outpost", ox - R, oy - R, ox + R, oy - R, mat="brick")
    wall_line("outpost", ox + R, oy - R, ox + R, oy + R, mat="brick")
    wall_line("outpost", ox + R, oy + R, ox - R, oy + R, mat="brick")
    wall_line("outpost", ox - R, oy + R, ox - R, oy - R, mat="brick", door_at=2)  # 서면(마을 쪽) 문
    for sx, sy in [(-R, -R), (R, -R), (R, R), (-R, R)]:
        place("outpost", "pillar", ox + sx, oy + sy, mat="basalt", scale=(1.5, 1.5, 1.3))
        fire("outpost", ox + sx * 0.9, oy + sy * 0.9)
    place("outpost", "table", ox, oy, mat="wood")
    place("outpost", "cube", ox + 300, oy + 300, scale=(0.6, 0.6, 0.6), mat="wood", z_off=30)
    # 포로 우리
    cx, cy = ox + 600, oy + 400
    ring("outpost", cx, cy, 250, 6, mesh="pillar", mat="rust", scale=(0.6, 0.6, 0.7))
    trigger("outpost", ox, oy, ext=(1000, 1000, 250))
    move_npc("Elara", cx, cy, ox, oy)
    move_npc("Commander_Vorg", ox - 200, oy - 200, ox - R, oy)

    # ── citadel: 대형 벽·첨탑·왕좌·결계 ──
    zx, zy = 14000.0, 7500.0
    W = 1500
    for x0, y0, x1, y1 in [
        (zx - W, zy - W, zx + W, zy - W),
        (zx + W, zy - W, zx + W, zy + W),
        (zx + W, zy + W, zx - W, zy + W),
    ]:
        wall_line("citadel", x0, y0, x1, y1, seg=500, mesh="wall5", mat="basalt")
        # 2층
        dx, dy = x1 - x0, y1 - y0
        n = max(1, int(round(math.hypot(dx, dy) / 500)))
        yaw = math.degrees(math.atan2(dy, dx))
        for i in range(n):
            t = i / n
            place("citadel", "wall5", x0 + dx * t, y0 + dy * t, yaw=yaw, mat="basalt", z_off=500)
    for sx, sy in [(-W, -W), (W, -W), (W, W), (-W, W)]:
        place("citadel", "pyramid", zx + sx, zy + sy, scale=(3.0, 3.0, 9.0), mat="basalt")
    fire("citadel", zx - W + 300, zy - W + 300, scale=1.6)
    fire("citadel", zx + W - 300, zy - W + 300, scale=1.6)
    floor_grid("citadel", zx, zy, 4, 4, mat="basalt")
    place("citadel", "cube", zx, zy + 900, scale=(3.0, 2.0, 0.6), mat="basalt", z_off=2)
    place("citadel", "chair", zx, zy + 900, yaw=-90, scale=(2.0, 2.0, 2.0), mat="rust", z_off=62)
    # 결계
    kx, ky = zx - 600, zy - 500
    place("citadel", "torus", kx, ky, scale=(4.0, 4.0, 0.3), mat="gold", z_off=10)
    ring("citadel", kx, ky, 300, 5, mesh="pillar", mat="rust", scale=(0.5, 0.5, 0.8))
    trigger("citadel", zx, zy, ext=(1500, 1500, 300))
    move_npc("DemonLord", zx, zy + 700, zx, zy - W)

    # ── NavMesh 확대 ──
    nav = find_actor(cls="NavMeshBoundsVolume")
    if nav:
        nav.set_actor_location(unreal.Vector(5000, 2500, 300), False, False)
        nav.set_actor_scale3d(unreal.Vector(110, 80, 12))  # BrushComponent 기본 200 유닛 → 22000×16000×2400
        print("[scene] NavMeshBounds →", nav.get_actor_location(), nav.get_actor_scale3d())


with unreal.ScopedEditorTransaction("Story: 7구역 씬 빌드"):
    clear_scene()
    build()

print("[scene] counts:", COUNTS)
print("[scene] save:", LES.save_current_level())

"""스토리 전 맵 빌더 — Sample 레벨 500m×500m 전체 (2026-09-18 v2). 멱등: 라벨 SCN_* 액터 전부 삭제 후 재생성.

에디터 안에서 실행: MCP `ue_run_python(mode="file", script="<이 파일 절대경로>")` 또는 에디터 Python 콘솔
`py "C:/github/UE5_MCP_VR/tools/build_story_scene.py"`.
전제: Content/StarterContent (Engine/Samples 에서 복사, Memo 참조) · C++ AStoryZoneTrigger · BP_HISMCluster(이 스크립트가 없으면 생성).

동선(시작→끝):
  ruins(불타는 성 폐허, PlayerStart) → gate(성문) → plaza(광장·제단) → hideout(은신처) → 서문 → library(도서관 폐허)
  → 남문 → forest(숲·약초·폭격 자리) → bridge(강·다리) → outpost(목책 전초기지·포로 우리) → 북문 → 죽은 숲 → citadel(대성채)
구조물 = StaticMeshActor, 식생·바위·잔해 = BP_HISMCluster(HISM). 손으로 옮긴 소품은 재실행 시 초기화 — 좌표는 여기서 고친다.
"""

import math
import random

import unreal

random.seed(11)

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

SC = "/Game/StarterContent"
MESH = {
    "wall": f"{SC}/Architecture/Wall_400x400",
    "wall3": f"{SC}/Architecture/Wall_400x300",
    "wall5": f"{SC}/Architecture/Wall_500x500",
    "door": f"{SC}/Architecture/Wall_Door_400x400",
    "window": f"{SC}/Architecture/Wall_Window_400x400",
    "floor": f"{SC}/Architecture/Floor_400x400",
    "pillar": f"{SC}/Architecture/Pillar_50x500",
    "rock": f"{SC}/Props/SM_Rock",
    "bush": f"{SC}/Props/SM_Bush",
    "chair": f"{SC}/Props/SM_Chair",
    "table": f"{SC}/Props/SM_TableRound",
    "shelf": f"{SC}/Props/SM_Shelf",
    "statue": f"{SC}/Props/SM_Statue",
    "lamp": f"{SC}/Props/SM_Lamp_Wall",
    "stairs": f"{SC}/Props/SM_Stairs",
    "cone": f"{SC}/Shapes/Shape_Cone",
    "cyl": f"{SC}/Shapes/Shape_Cylinder",
    "cube": f"{SC}/Shapes/Shape_Cube",
    "pyramid": f"{SC}/Shapes/Shape_QuadPyramid",
    "wedge": f"{SC}/Shapes/Shape_Wedge_A",
    "torus": f"{SC}/Shapes/Shape_Torus",
    "plane": f"{SC}/Shapes/Shape_Plane",
}
MAT = {
    "stone": f"{SC}/Materials/M_CobbleStone_Rough",
    "pebble": f"{SC}/Materials/M_CobbleStone_Pebble",
    "brick": f"{SC}/Materials/M_Brick_Clay_Old",
    "hewn": f"{SC}/Materials/M_Brick_Hewn_Stone",
    "cut": f"{SC}/Materials/M_Brick_Cut_Stone",
    "wall": f"{SC}/Materials/M_Basic_Wall",
    "basalt": f"{SC}/Materials/M_Rock_Basalt",
    "wood": f"{SC}/Materials/M_Wood_Oak",
    "pine": f"{SC}/Materials/M_Wood_Pine",
    "walnut": f"{SC}/Materials/M_Wood_Walnut",
    "grass": f"{SC}/Materials/M_Ground_Grass",
    "rust": f"{SC}/Materials/M_Metal_Rust",
    "gold": f"{SC}/Materials/M_Metal_Gold",
    "water": f"{SC}/Materials/M_Water_Lake",
}
FIRE = f"{SC}/Particles/P_Fire"
SMOKE = f"{SC}/Particles/P_Smoke"
HISM_BP = "/Game/Blueprint/Scene/BP_HISMCluster"

_asset_cache = {}
COUNTS = {}
HISM_TOTAL = {}


def load(path):
    if path not in _asset_cache:
        a = unreal.EditorAssetLibrary.load_asset(path)
        if a is None:
            raise RuntimeError(f"asset missing: {path}")
        _asset_cache[path] = a
    return _asset_cache[path]


def ground_z(x, y):
    hit = unreal.SystemLibrary.line_trace_single(
        WORLD, unreal.Vector(x, y, 5000), unreal.Vector(x, y, -5000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True,
    ).to_tuple()
    if hit[0] and hit[9] and "Landscape" in hit[9].get_class().get_name():
        return hit[5].z
    return 0.0


def _label(zone):
    COUNTS[zone] = COUNTS.get(zone, 0) + 1
    return f"SCN_{zone}_{COUNTS[zone]:04d}"


def _finish(a, label):
    a.set_actor_label(label)
    a.set_editor_property("is_spatially_loaded", False)
    return a


# ── 기본 배치 프리미티브 ─────────────────────────────────────────────
def place(zone, mesh_key, x, y, yaw=0.0, scale=(1, 1, 1), mat=None, z_off=0.0, pitch=0.0, roll=0.0, collision=True):
    z = ground_z(x, y) + z_off
    # unreal.Rotator 위치 인자는 (roll, pitch, yaw) — 키워드로만 넘긴다
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
    a.set_mobility(unreal.ComponentMobility.STATIC)
    smc = a.static_mesh_component
    smc.set_static_mesh(load(MESH[mesh_key]))
    if mat:
        smc.set_material(0, load(MAT[mat]))
    if not collision:
        smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_scale3d(unreal.Vector(*scale))
    return _finish(a, _label(zone))


def emitter(zone, template, x, y, z_off, scale=1.0):
    a = EAS.spawn_actor_from_class(unreal.Emitter, unreal.Vector(x, y, ground_z(x, y) + z_off), unreal.Rotator(0, 0, 0))
    a.set_template(load(template))
    a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    return _finish(a, _label(zone))


def torch(zone, x, y, scale=1.0):
    place(zone, "cyl", x, y, scale=(0.3, 0.3, 1.1), mat="rust")
    emitter(zone, FIRE, x, y, 120.0, scale)


def campfire(zone, x, y, scale=1.5):
    for i in range(6):
        ang = 2 * math.pi * i / 6
        place(zone, "rock", x + 90 * math.cos(ang), y + 90 * math.sin(ang), yaw=random.uniform(0, 360), scale=(0.25, 0.25, 0.15))
    emitter(zone, FIRE, x, y, 10.0, scale)


CHAIR_BP = "/Game/Blueprint/FurnitureActor/BP_Chair"
_chair_n = 0


def chair(zone, x, y, yaw=0.0, scale=1.0, fid=None):
    """앉을 수 있는 의자 = BP_Chair(FurnitureActor, SM_Chair). yaw = 앉은 사람이 보는 방향(+X 기준).
    FurnitureID 는 LLM valid_targets 로 노출되니 의미 있는 이름으로."""
    global _chair_n
    _chair_n += 1
    cls = unreal.EditorAssetLibrary.load_blueprint_class(CHAIR_BP)
    a = EAS.spawn_actor_from_class(cls, unreal.Vector(x, y, ground_z(x, y)), unreal.Rotator(yaw=yaw))
    a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    a.set_editor_property("FurnitureID", fid or f"Chair_{zone}_{_chair_n:02d}")
    return _finish(a, _label(zone) + "_chair")


def trigger(zone, x, y, ext=(450, 450, 200)):
    a = EAS.spawn_actor_from_class(unreal.StoryZoneTrigger, unreal.Vector(x, y, ground_z(x, y) + 100), unreal.Rotator(0, 0, 0))
    a.set_editor_property("ZoneName", zone)
    a.get_editor_property("Box").set_box_extent(unreal.Vector(*ext))
    return _finish(a, f"SCN_{zone}_trigger")


def wall_line(zone, x0, y0, x1, y1, seg=400.0, mesh="wall", mat="stone", door_at=(), window_every=0, z_off=0.0):
    """(x0,y0)→(x1,y1) 직선 벽. Wall_400x400 피벗은 시작 모서리라 세그먼트가 이어 붙는다. door_at: 문 세그먼트 인덱스들."""
    dx, dy = x1 - x0, y1 - y0
    n = max(1, int(round(math.hypot(dx, dy) / seg)))
    yaw = math.degrees(math.atan2(dy, dx))
    for i in range(n):
        t = i / n
        key = "door" if i in door_at else ("window" if window_every and i % window_every == window_every - 1 else mesh)
        place(zone, key, x0 + dx * t, y0 + dy * t, yaw=yaw, mat=mat, z_off=z_off)


def rect_walls(zone, cx, cy, hw, hh, mat="stone", doors=None, seg=400.0, mesh="wall", z_off=0.0, window_every=0):
    """직사각형 4면. doors = {"n"|"s"|"e"|"w": 세그먼트 인덱스}. 시계 방향, 안쪽이 벽 정면."""
    doors = doors or {}
    wall_line(zone, cx - hw, cy - hh, cx + hw, cy - hh, seg, mesh, mat, (doors.get("s"),), window_every, z_off)  # 남
    wall_line(zone, cx + hw, cy - hh, cx + hw, cy + hh, seg, mesh, mat, (doors.get("e"),), window_every, z_off)  # 동
    wall_line(zone, cx + hw, cy + hh, cx - hw, cy + hh, seg, mesh, mat, (doors.get("n"),), window_every, z_off)  # 북
    wall_line(zone, cx - hw, cy + hh, cx - hw, cy - hh, seg, mesh, mat, (doors.get("w"),), window_every, z_off)  # 서


def floor_grid(zone, cx, cy, nx, ny, mat="stone", z_off=2.0):
    for i in range(nx):
        for j in range(ny):
            # Floor_400x400 피벗은 모서리(0..400) — 중심 정렬 -200
            place(zone, "floor", cx + (i - (nx - 1) / 2) * 400 - 200, cy + (j - (ny - 1) / 2) * 400 - 200, mat=mat, z_off=z_off)


def ring(zone, cx, cy, r, n, mesh="pillar", mat="stone", scale=(1, 1, 1), tilt=0.0, z_off=0.0):
    for i in range(n):
        ang = 2 * math.pi * i / n
        place(zone, mesh, cx + r * math.cos(ang), cy + r * math.sin(ang), yaw=math.degrees(ang), scale=scale, mat=mat,
              roll=random.uniform(-tilt, tilt), pitch=random.uniform(-tilt, tilt), z_off=z_off)


def tower(zone, x, y, h=2, mat="hewn", roof="pyramid", roof_mat="basalt", fire_top=True):
    """망루/성탑: 400 각 기둥형 사각 벽 h 층 + 지붕."""
    for lvl in range(h):
        rect_walls(zone, x, y, 200, 200, mat=mat, z_off=lvl * 400)
    place(zone, roof, x, y, scale=(4.6, 4.6, 3.0), mat=roof_mat, z_off=h * 400)
    if fire_top:
        emitter(zone, FIRE, x, y, h * 400 + 300, 0.8)


def house(zone, x, y, yaw=0.0, w=1, d=1, mat="brick", roof_mat="walnut", door_side="s", window=True):
    """집: (w×400)×(d×400) 평면, 4면 벽, 문 1, 창, 쐐기 지붕. yaw 는 집 전체 회전(0/90/180/270 권장)."""
    hw, hh = w * 200, d * 200
    rad = math.radians(yaw)

    def R(px, py):
        return x + px * math.cos(rad) - py * math.sin(rad), y + px * math.sin(rad) + py * math.cos(rad)

    floor_grid(zone, x, y, w, d, mat="pine", z_off=1.0)
    sides = {"s": ((-hw, -hh), (hw, -hh)), "e": ((hw, -hh), (hw, hh)), "n": ((hw, hh), (-hw, hh)), "w": ((-hw, hh), (-hw, -hh))}
    for side, ((ax, ay), (bx, by)) in sides.items():
        n = max(1, int(round(math.hypot(bx - ax, by - ay) / 400)))
        for i in range(n):
            t = i / n
            px, py = ax + (bx - ax) * t, ay + (by - ay) * t
            wx, wy = R(px, py)
            wyaw = math.degrees(math.atan2(by - ay, bx - ax)) + yaw
            key = "door" if (side == door_side and i == n // 2) else ("window" if window and i == 0 and side != door_side else "wall")
            place(zone, key, wx, wy, yaw=wyaw, mat=mat)
    # 지붕: 쐐기 2개 맞대기 (Shape_Wedge_A 100 각, 피벗 모서리)
    rx, ry = R(0, 0)
    place(zone, "wedge", rx, ry, yaw=yaw + 90, scale=(hh / 50, hw / 50 * 1.05, 2.2), mat=roof_mat, z_off=400)
    place(zone, "wedge", rx, ry, yaw=yaw - 90, scale=(hh / 50, hw / 50 * 1.05, 2.2), mat=roof_mat, z_off=400)


def road(zone, pts, width=1, mat="pebble"):
    """폴리라인 따라 400 타일 자갈길. width 타일 폭."""
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy)
        n = max(1, int(L / 400))
        yaw = math.degrees(math.atan2(dy, dx))
        nx, ny = -dy / L, dx / L
        for i in range(n):
            t = i / n
            for k in range(width):
                off = (k - (width - 1) / 2) * 400
                px, py = x0 + dx * t + nx * off, y0 + dy * t + ny * off
                # 피벗 모서리 보정: 진행방향 뒤로 0, 법선 -200
                place(zone, "floor", px - nx * 200, py - ny * 200, yaw=yaw, mat=mat, z_off=1.0)


# ── HISM 클러스터 ────────────────────────────────────────────────────
def ensure_hism_bp():
    if unreal.EditorAssetLibrary.does_asset_exist(HISM_BP):
        return
    path, name = HISM_BP.rsplit("/", 1)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path, unreal.Blueprint, factory)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = sds.k2_gather_subobject_data_for_blueprint(bp)[0]
    handle, _ = sds.add_new_subobject(unreal.AddNewSubobjectParams(
        parent_handle=root, new_class=unreal.HierarchicalInstancedStaticMeshComponent, blueprint_context=bp))
    sds.rename_subobject(handle, unreal.Text("HISM"))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)


def hism(zone, mesh_key, transforms, mat=None, collision=True):
    """transforms: [(x, y, z_off, yaw, sx, sy, sz)]. 1 액터 = 1 메시 클러스터."""
    if not transforms:
        return None
    cls = unreal.EditorAssetLibrary.load_blueprint_class(HISM_BP)
    a = EAS.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    h = a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0]
    h.set_static_mesh(load(MESH[mesh_key]))
    if mat:
        h.set_material(0, load(MAT[mat]))
    if not collision:
        h.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    h.set_mobility(unreal.ComponentMobility.STATIC)
    xs = [unreal.Transform(unreal.Vector(x, y, ground_z(x, y) + zo), unreal.Rotator(yaw=yaw), unreal.Vector(sx, sy, sz))
          for (x, y, zo, yaw, sx, sy, sz) in transforms]
    h.add_instances(xs, False, True, True)
    HISM_TOTAL[zone] = HISM_TOTAL.get(zone, 0) + len(xs)
    return _finish(a, _label(zone) + "_hism_" + mesh_key)


def forest_patch(zone, cx, cy, r, n_trees, dead=False, density_bush=0.6, density_rock=0.12, exclude=()):
    """원형 숲. exclude: [(x,y,r)] 빈 자리. dead=True 면 잎 없는 검은 나무."""

    def ok(x, y):
        return all(math.hypot(x - ex, y - ey) > er for ex, ey, er in exclude)

    trunks, crowns, bushes, rocks = [], [], [], []
    for _ in range(n_trees):
        ang, rr = random.uniform(0, 2 * math.pi), r * math.sqrt(random.random())
        x, y = cx + rr * math.cos(ang), cy + rr * math.sin(ang)
        if not ok(x, y):
            continue
        h = random.uniform(2.6, 4.6)
        tw = random.uniform(0.35, 0.6)
        trunks.append((x, y, 0, random.uniform(0, 360), tw, tw, h))
        if not dead:
            cw = random.uniform(2.4, 3.6)
            crowns.append((x, y, h * 100 * 0.5, random.uniform(0, 360), cw, cw, random.uniform(3.0, 4.2)))
            if random.random() < 0.5:
                crowns.append((x, y, h * 100 * 0.78, random.uniform(0, 360), cw * 0.7, cw * 0.7, random.uniform(2.2, 3.0)))
    for _ in range(int(n_trees * density_bush)):
        ang, rr = random.uniform(0, 2 * math.pi), r * math.sqrt(random.random())
        x, y = cx + rr * math.cos(ang), cy + rr * math.sin(ang)
        if ok(x, y):
            s = random.uniform(0.7, 1.6)
            bushes.append((x, y, 0, random.uniform(0, 360), s, s, s))
    for _ in range(int(n_trees * density_rock)):
        ang, rr = random.uniform(0, 2 * math.pi), r * random.random()
        x, y = cx + rr * math.cos(ang), cy + rr * math.sin(ang)
        if ok(x, y):
            s = random.uniform(0.5, 1.8)
            rocks.append((x, y, 0, random.uniform(0, 360), s, s, s * random.uniform(0.6, 1.2)))
    hism(zone, "cyl", trunks, mat="basalt" if dead else "wood")
    if crowns:
        hism(zone, "cone", crowns, mat="grass")
    if bushes and not dead:
        hism(zone, "bush", bushes, collision=False)
    hism(zone, "rock", rocks)


def scatter(zone, cx, cy, hw, hh, n, mesh_key, mat=None, smin=0.6, smax=1.6, exclude=(), collision=True, zflat=0.6):
    pts = []
    for _ in range(n):
        x, y = random.uniform(cx - hw, cx + hw), random.uniform(cy - hh, cy + hh)
        if all(math.hypot(x - ex, y - ey) > er for ex, ey, er in exclude):
            s = random.uniform(smin, smax)
            pts.append((x, y, 0, random.uniform(0, 360), s, s, s * random.uniform(zflat, 1.2)))
    hism(zone, mesh_key, pts, mat=mat, collision=collision)


# ── 레벨 액터 유틸 ────────────────────────────────────────────────────
def clear_scene():
    n = 0
    for a in EAS.get_all_level_actors():
        if a.get_actor_label().startswith("SCN_"):
            EAS.destroy_actor(a)
            n += 1
    print("[scene] cleared SCN_ actors:", n)


def find_actor(agent_id=None, cls=None):
    for a in EAS.get_all_level_actors():
        if agent_id and a.get_class().get_name() == "BP_SmartNPC_C" and a.get_editor_property("AgentID") == agent_id:
            return a
        if cls and a.get_class().get_name() == cls:
            return a
    return None


def move_actor(a, x, y, z_off=0.0, face=None, yaw=None):
    z = ground_z(x, y) + z_off
    loc = unreal.Vector(x, y, z)
    a.set_actor_location(loc, False, False)
    if face is not None:
        yaw = unreal.MathLibrary.find_look_at_rotation(loc, unreal.Vector(face[0], face[1], z)).yaw
    if yaw is not None:
        a.set_actor_rotation(unreal.Rotator(yaw=yaw), False)
    return loc


def move_npc(agent_id, x, y, face=None):
    a = find_actor(agent_id=agent_id)
    if not a:
        print("[scene] NPC 없음:", agent_id)
        return
    print("[scene] NPC", agent_id, "→", move_actor(a, x, y, 88, face))


# ═════════════════════════════════════════════════════════════════════
# 레이아웃 상수 (cm). 맵 ±25000.
VIL = (-1000.0, -2000.0)       # 마을 중심
VIL_HW, VIL_HH = 4200.0, 2600.0  # 마을 성벽 반폭/반높이 → 84m × 52m
GATE_N = (VIL[0], VIL[1] + VIL_HH)      # 북문 (-1000, 600)
GATE_W = (VIL[0] - VIL_HW, VIL[1])      # 서문 (-5200, -2000)
GATE_S = (VIL[0] + 1400, VIL[1] - VIL_HH)  # 남문 (400, -4600)
PLAZA = (VIL[0], VIL[1] + 200)          # 광장 (-1000, -1800)
HIDEOUT = (VIL[0] - 2600, VIL[1] - 1400)  # 은신처 (-3600, -3400)
RUINS = (-1000.0, 6500.0)                # 불타는 성 폐허(시작)
LIBRARY = (-10500.0, -3200.0)
FOREST = (7500.0, -7500.0)
FOREST_R = 5200.0
CLEARING = (7000.0, -6800.0)             # 약초 빈터
BOMB = (11000.0, -4200.0)                # 폭격 자리(Skadi)
RIVER_X = 13800.0                        # 강 중심선 x, 남북
BRIDGE = (RIVER_X, -1500.0)
OUTPOST = (19000.0, -500.0)  # 서벽 x=16800 — 다리(12400..15200) 와 안 겹치게
OUT_HW = 2200.0
CITADEL = (17500.0, 12500.0)
CIT_HW = 3800.0


def build_village():
    z = "village"
    # 성벽 + 모서리 탑 + 문 3 (북=정문, 서, 남)
    cx, cy = VIL
    n_seg_x = int(2 * VIL_HW / 400)
    rect_walls(z, cx, cy, VIL_HW, VIL_HH, mat="hewn",
               doors={"n": n_seg_x // 2, "w": int(2 * VIL_HH / 400) // 2, "s": int((GATE_S[0] - (cx - VIL_HW)) / 400)})
    rect_walls(z, cx, cy, VIL_HW, VIL_HH, mat="hewn", z_off=400)  # 2층
    for sx, sy in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
        tower(z, cx + sx * VIL_HW, cy + sy * VIL_HH, h=3)
    # 정문 성문루: 문 양옆 큰 기둥 + 위 통로 + 횃불
    gx, gy = GATE_N
    for sx in (-1, 1):
        tower(z, gx + sx * 700, gy, h=3, fire_top=True)
    floor_grid(z, gx, gy, 3, 1, mat="hewn", z_off=800)
    torch(z, gx - 420, gy - 250)
    torch(z, gx + 420, gy - 250)
    # 서문·남문 횃불
    for (dx, dy) in [GATE_W, GATE_S]:
        torch(z, dx + 300 if dy == GATE_W[1] else dx - 300, dy + 300 if dx == GATE_S[0] else dy - 300)
    # 광장: 바닥·제단·기둥·벤치·우물
    px, py = PLAZA
    floor_grid(z, px, py, 5, 5, mat="stone")
    place(z, "cube", px, py, scale=(2.4, 2.4, 0.35), mat="basalt", z_off=2)
    place(z, "statue", px, py, z_off=37, scale=(1.4, 1.4, 1.4), mat="gold")
    ring(z, px, py, 700, 6, mesh="pillar", mat="stone")
    for ang in (45, 135, 225, 315):
        r = math.radians(ang)
        chair(z, px + 850 * math.cos(r), py + 850 * math.sin(r), yaw=ang + 180, fid=f"Chair_Plaza_{ang}")
    # 우물
    wx, wy = px + 1300, py - 900
    place(z, "torus", wx, wy, scale=(1.6, 1.6, 1.2), mat="cut", z_off=0)
    place(z, "cyl", wx, wy, scale=(0.9, 0.9, 0.15), mat="water", z_off=60, collision=False)
    place(z, "pillar", wx - 70, wy, scale=(0.4, 0.4, 0.5), mat="wood")
    place(z, "pillar", wx + 70, wy, scale=(0.4, 0.4, 0.5), mat="wood")
    place(z, "wedge", wx, wy, yaw=90, scale=(1.6, 1.4, 1.0), mat="walnut", z_off=250)
    trigger("plaza", px, py, ext=(1000, 1000, 250))
    # 집들 (광장·길·은신처 피해서)
    houses = [
        (-3200, -900, 0, 2, 1), (-3200, 200, 0, 1, 1), (800, 200, 180, 2, 1), (1600, -800, 90, 1, 2),
        (2400, -2600, 90, 1, 2), (-2400, -3600, 0, 2, 1), (500, -3700, 0, 1, 1), (2600, 100, 180, 1, 1),
        (-1000, -4000, 0, 2, 1), (1800, -3900, 180, 1, 1),
    ]
    for (hx, hy, yaw, w, d) in houses:
        house(z, hx, hy, yaw=yaw, w=w, d=d, mat=random.choice(["brick", "wall", "cut"]),
              roof_mat=random.choice(["walnut", "pine", "basalt"]), door_side="s")
    # 시장: 가판대(테이블+천장 쐐기) + 상자·통
    for i, (mx, my) in enumerate([(-200, -3000), (400, -3000), (1000, -3000)]):
        place(z, "table", mx, my, mat="wood")
        place(z, "pillar", mx - 150, my + 150, scale=(0.3, 0.3, 0.5), mat="wood")
        place(z, "pillar", mx + 150, my + 150, scale=(0.3, 0.3, 0.5), mat="wood")
        place(z, "wedge", mx, my + 150, yaw=90, scale=(1.5, 3.4, 0.8), mat=random.choice(["rust", "gold", "walnut"]), z_off=250)
    crates = [(mx + random.uniform(-500, 500), my + random.uniform(-200, 500), 0, random.uniform(0, 360), s, s, s)
              for (mx, my) in [(-3000, -2200), (1900, -1500), (-600, -3300)] for s in [random.uniform(0.5, 0.9) for _ in range(4)]]
    hism(z, "cube", crates, mat="wood")
    barrels = [(bx + random.uniform(-300, 300), by + random.uniform(-300, 300), 0, 0, 0.5, 0.5, 0.8)
               for (bx, by) in [(-2800, -1900), (2200, -1300), (900, -3500)] for _ in range(3)]
    hism(z, "cyl", barrels, mat="walnut")
    # 가로등 횃불
    for (tx, ty) in [(-2000, -1000), (600, -1000), (-2000, -3000), (1700, -300), (-3600, -2000), (2600, -3600)]:
        torch(z, tx, ty)
    # 은신처: 2×2 건물 + 작업대·서가·침대
    hx, hy = HIDEOUT
    house(z, hx, hy, yaw=0, w=2, d=2, mat="cut", roof_mat="basalt", door_side="n", window=False)
    place(z, "table", hx, hy - 150, mat="wood")
    place(z, "shelf", hx - 300, hy - 300, yaw=90, mat="wood")
    place(z, "shelf", hx + 300, hy - 300, yaw=-90, mat="wood")
    place(z, "lamp", hx + 380, hy + 100, yaw=180, z_off=220)
    bed = find_actor(cls="BP_Bed_C")
    if bed:
        move_actor(bed, hx + 180, hy + 200, 0, yaw=90)
    trigger("hideout", hx, hy, ext=(420, 420, 250))
    # 성문 트리거 (문 바로 안쪽)
    trigger("gate", gx, gy - 300, ext=(800, 300, 250))
    # NPC
    move_npc("Guard", gx + 250, gy - 500, face=(gx, gy + 900))
    move_npc("James", px + 400, py + 350, face=PLAZA)


def build_ruins():
    """불타는 성 폐허 — 시작 지점. 무너진 벽·불·연기·잔해."""
    z = "ruins"
    rx, ry = RUINS
    floor_grid(z, rx, ry, 5, 5, mat="hewn")
    # 무너진 외벽(반쯤만), 기울어진 기둥, 잔해
    wall_line(z, rx - 1600, ry + 1200, rx + 600, ry + 1200, mat="hewn")
    wall_line(z, rx + 1600, ry + 1200, rx + 1600, ry - 400, mat="hewn")
    wall_line(z, rx - 1600, ry + 1200, rx - 1600, ry - 800, mat="hewn")
    for i in range(3):
        wall_line(z, rx - 1600 + i * 400, ry - 1200, rx - 1200 + i * 400, ry - 1200, mat="hewn")
    ring(z, rx, ry, 900, 8, mesh="pillar", mat="hewn", tilt=14.0)
    for (dx, dy, yaw, pitch) in [(-800, 300, 20, -35), (700, -500, -30, 40), (300, 800, 60, -50)]:
        place(z, "wall3", rx + dx, ry + dy, yaw=yaw, pitch=pitch, mat="hewn")
    tower(z, rx - 1600, ry + 1200, h=2, fire_top=True)
    tower(z, rx + 1600, ry + 1200, h=1, fire_top=True)
    # 불·연기
    for (dx, dy, s) in [(-500, 200, 2.2), (600, 500, 1.8), (200, -700, 2.5), (-1100, -300, 1.6), (1000, -200, 2.0)]:
        emitter(z, FIRE, rx + dx, ry + dy, 10, s)
        emitter(z, SMOKE, rx + dx, ry + dy, 200, s)
    debris = [(rx + random.uniform(-1500, 1500), ry + random.uniform(-1100, 1100), 0, random.uniform(0, 360), s, s, s * 0.6)
              for s in [random.uniform(0.4, 1.3) for _ in range(40)]]
    hism(z, "cube", debris, mat="basalt")
    hism(z, "rock", [(rx + random.uniform(-1700, 1700), ry + random.uniform(-1300, 1300), 0, random.uniform(0, 360), s, s, s)
                     for s in [random.uniform(0.6, 1.5) for _ in range(14)]])
    # 왕좌홀 흔적: 제단 + 부서진 조각상
    place(z, "cube", rx, ry, scale=(2.0, 2.0, 0.3), mat="basalt", z_off=2)
    place(z, "statue", rx, ry, z_off=32, scale=(1.3, 1.3, 1.3), mat="rust", pitch=25)
    trigger("ruins", rx, ry, ext=(1600, 1300, 300))
    ps = find_actor(cls="PlayerStart")
    if ps:
        print("[scene] PlayerStart →", move_actor(ps, rx, ry + 600, 100, yaw=-90))  # 폐허 안, 남향 → 성문


def build_library():
    z = "library"
    lx, ly = LIBRARY
    floor_grid(z, lx, ly, 6, 4, mat="stone")
    # 열주 2열 + 부서진 지붕 조각 + 서가 회랑
    for i in range(6):
        for sy in (-1, 1):
            place(z, "pillar", lx - 1000 + i * 400, ly + sy * 600, mat="stone", scale=(1.2, 1.2, 1.4),
                  roll=random.uniform(-10, 10) if random.random() < 0.4 else 0)
    wall_line(z, lx - 1300, ly + 900, lx + 1300, ly + 900, mat="brick", mesh="wall3", window_every=2)
    wall_line(z, lx - 1300, ly - 900, lx - 500, ly - 900, mat="brick", mesh="wall3")
    wall_line(z, lx + 400, ly - 900, lx + 1300, ly - 900, mat="brick", mesh="wall3")
    wall_line(z, lx - 1300, ly + 900, lx - 1300, ly - 900, mat="brick", mesh="wall3", window_every=2)
    for (dx, dy, yaw, roll) in [(-300, 550, 0, -8), (100, 550, 0, 0), (500, 550, 0, 6), (-1250, 200, 90, 0), (-1250, -200, 90, -5),
                                (900, 550, 0, 0), (-700, 550, 0, 0)]:
        place(z, "shelf", lx + dx, ly + dy, yaw=yaw, roll=roll, mat="wood")
    for (dx, dy, yaw, pitch) in [(300, -300, 30, -40), (-900, 100, -20, 30)]:
        place(z, "wall3", lx + dx, ly + dy, yaw=yaw, pitch=pitch, mat="brick")
    place(z, "statue", lx, ly - 400, scale=(1.5, 1.5, 1.5), mat="stone")
    chair(z, lx + 60, ly + 80, yaw=-90, fid="Chair_Library_Reading")  # 탁자(-Y) 를 본다
    place(z, "table", lx + 60, ly - 120, mat="wood")
    hism(z, "cube", [(lx + random.uniform(-1200, 1200), ly + random.uniform(-800, 800), 0, random.uniform(0, 360), 0.25, 0.35, 0.08)
                     for _ in range(60)], mat="pine")  # 흩어진 책
    hism(z, "rock", [(lx + random.uniform(-1800, 1800), ly + random.uniform(-1400, 1400), 0, random.uniform(0, 360), s, s, s)
                     for s in [random.uniform(0.5, 1.4) for _ in range(12)]])
    torch(z, lx - 1300, ly - 1100)
    torch(z, lx + 1300, ly - 1100)
    trigger("library", lx, ly, ext=(1400, 1000, 300))
    move_npc("Moca", lx + 60, ly + 190, face=(lx + 60, ly - 120))


def build_forest():
    z = "forest"
    fx, fy = FOREST
    cxp, cyp = CLEARING
    bx, by = BOMB
    forest_patch(z, fx, fy, FOREST_R, 520, exclude=[(cxp, cyp, 900), (bx, by, 900), (GATE_S[0] + 2500, GATE_S[1] - 1500, 900)])
    # 약초 빈터: 바위 원 + 약초(초록 덤불) + 드롭
    ring(z, cxp, cyp, 700, 7, mesh="rock", scale=(1.0, 1.0, 0.7))
    hism(z, "bush", [(cxp + random.uniform(-500, 500), cyp + random.uniform(-500, 500), 0, random.uniform(0, 360), 0.5, 0.5, 0.5)
                     for _ in range(16)], collision=False)
    drop_cls = unreal.EditorAssetLibrary.load_blueprint_class("/Game/Blueprint/Entity/BP_DropItem")
    d = EAS.spawn_actor_from_class(drop_cls, unreal.Vector(cxp, cyp, ground_z(cxp, cyp) + 20), unreal.Rotator(0, 0, 0))
    item = d.get_editor_property("ItemData")
    item.set_editor_property("item_template_id", "HerbBasket")
    d.set_editor_property("ItemData", item)
    _finish(d, "SCN_forest_HerbBasket")
    campfire(z, cxp + 350, cyp - 300, 1.0)
    trigger("forest", cxp, cyp, ext=(1200, 1200, 300))
    # 폭격 자리: 분화구(토러스) + 연기 + 잔해 + 부서진 마왕군 수레
    for (dx, dy, s) in [(0, 0, 3.0), (600, 400, 2.0), (-500, 500, 2.4)]:
        place(z, "torus", bx + dx, by + dy, scale=(s, s, 0.25), mat="basalt", z_off=0, collision=False)
        emitter(z, SMOKE, bx + dx, by + dy, 30, 1.5)
    emitter(z, FIRE, bx + 600, by + 400, 10, 1.2)
    hism(z, "cube", [(bx + random.uniform(-900, 900), by + random.uniform(-900, 900), 0, random.uniform(0, 360), s, s, s * 0.5)
                     for s in [random.uniform(0.3, 1.0) for _ in range(30)]], mat="basalt")
    place(z, "cube", bx + 900, by - 300, yaw=30, scale=(2.0, 1.2, 0.8), mat="walnut", roll=25)  # 뒤집힌 수레
    place(z, "torus", bx + 700, by - 200, yaw=90, scale=(0.8, 0.8, 0.2), mat="rust", pitch=90)
    place(z, "rock", bx - 700, by - 600, scale=(2.4, 2.4, 1.3))  # Skadi 가 올라선 바위
    move_npc("Skadi", bx - 700, by - 250, face=OUTPOST)


def build_river_bridge():
    z = "bridge"
    # 강: 남북 긴 수면 (충돌 없음) + 강둑 바위
    for i in range(-6, 7):
        place(z, "plane", RIVER_X, i * 4000, scale=(9.0, 40.0, 1.0), mat="water", z_off=4, collision=False)
    scatter(z, RIVER_X, 0, 900, 24000, 260, "rock", smin=0.4, smax=1.3, exclude=[(BRIDGE[0], BRIDGE[1], 1400)], zflat=0.4)
    # 다리: 상판 + 난간 기둥 + 교각
    bx, by = BRIDGE
    for i in range(-3, 4):
        place(z, "floor", bx - 200 + i * 400, by - 200, mat="pine", z_off=60)
        place(z, "floor", bx - 200 + i * 400, by + 200, mat="pine", z_off=60)
        if i % 2 == 0:
            for sy in (-1, 1):
                place(z, "pillar", bx + i * 400, by + sy * 420, scale=(0.5, 0.5, 0.25), mat="wood", z_off=60)
                place(z, "pillar", bx + i * 400, by + sy * 420, scale=(0.6, 0.6, 0.12), mat="basalt", z_off=0)
    place(z, "cube", bx, by, scale=(30.0, 1.0, 0.5), mat="pine", z_off=110)  # 난간 가로대(양쪽)
    place(z, "cube", bx, by - 440, scale=(28.0, 0.2, 0.15), mat="wood", z_off=170)
    place(z, "cube", bx, by + 440, scale=(28.0, 0.2, 0.15), mat="wood", z_off=170)
    torch(z, bx - 1500, by - 600)
    torch(z, bx + 1500, by + 600)
    trigger("bridge", bx, by, ext=(1500, 600, 300))


def build_outpost():
    z = "outpost"
    ox, oy = OUTPOST
    # 목책(통나무 기둥 촘촘) + 서문 + 망루 4
    posts = []
    for side in range(4):
        for t in [i / 44 for i in range(44)]:
            if side == 0:
                x, y = ox - OUT_HW + 2 * OUT_HW * t, oy - OUT_HW
            elif side == 1:
                x, y = ox + OUT_HW, oy - OUT_HW + 2 * OUT_HW * t
            elif side == 2:
                x, y = ox + OUT_HW - 2 * OUT_HW * t, oy + OUT_HW
            else:
                x, y = ox - OUT_HW, oy + OUT_HW - 2 * OUT_HW * t
                if abs(y - oy) < 450:
                    continue  # 서문
            posts.append((x, y, 0, random.uniform(0, 360), 0.55, 0.55, random.uniform(3.2, 3.8)))
    hism(z, "cyl", posts, mat="walnut")
    for sx, sy in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
        tower(z, ox + sx * OUT_HW, oy + sy * OUT_HW, h=2, mat="walnut", roof="wedge", roof_mat="rust")
    torch(z, ox - OUT_HW - 200, oy - 600)
    torch(z, ox - OUT_HW - 200, oy + 600)
    floor_grid(z, ox, oy, 4, 4, mat="pebble")
    # 막사 2 + 지휘 천막 + 무기대·상자·모닥불
    house(z, ox + 1200, oy + 1200, yaw=270, w=2, d=1, mat="walnut", roof_mat="rust", door_side="s")
    house(z, ox + 1200, oy - 1200, yaw=270, w=2, d=1, mat="walnut", roof_mat="rust", door_side="s")
    place(z, "pyramid", ox - 900, oy + 1100, scale=(9.0, 9.0, 3.5), mat="rust")  # 천막
    place(z, "table", ox, oy, mat="wood")
    campfire(z, ox - 500, oy - 900, 1.6)
    hism(z, "cube", [(ox + random.uniform(-1800, 1800), oy + random.uniform(-1800, 1800), 0, random.uniform(0, 360), s, s, s)
                     for s in [random.uniform(0.4, 0.9) for _ in range(18)]], mat="wood")
    hism(z, "cyl", [(ox + random.uniform(-1800, 1800), oy + random.uniform(-1800, 1800), 0, 0, 0.5, 0.5, 0.8) for _ in range(8)], mat="rust")
    # 마왕군 깃발: 기둥 + 검은 판
    for (fx_, fy_) in [(ox - OUT_HW + 500, oy - 900), (ox - OUT_HW + 500, oy + 900), (ox, oy + 1900)]:
        place(z, "pillar", fx_, fy_, scale=(0.35, 0.35, 1.3), mat="rust")
        place(z, "cube", fx_ + 60, fy_, scale=(0.06, 1.0, 1.4), mat="basalt", z_off=480, collision=False)
    # 포로 우리 (Elara)
    cx, cy = ox + 900, oy + 300
    ring(z, cx, cy, 300, 8, mesh="pillar", mat="rust", scale=(0.5, 0.5, 0.8))
    place(z, "torus", cx, cy, scale=(3.4, 3.4, 0.2), mat="rust", z_off=390, collision=False)
    trigger("outpost", ox - OUT_HW + 400, oy, ext=(500, 900, 300))  # 서문 안쪽
    move_npc("Elara", cx, cy, face=(ox, oy))
    move_npc("Commander_Vorg", ox - 300, oy - 300, face=(ox - OUT_HW, oy))


def build_citadel():
    z = "citadel"
    cx, cy = CITADEL
    H = CIT_HW
    # 단상(넓은 검은 대지) + 외벽 2층 + 성문루(남) + 모서리·중간 탑
    place(z, "cube", cx, cy, scale=(2 * H / 100 + 6, 2 * H / 100 + 6, 0.5), mat="basalt", z_off=-45)
    rect_walls(z, cx, cy, H, H, mat="basalt", seg=500, mesh="wall5", doors={"s": int(2 * H / 500) // 2})
    rect_walls(z, cx, cy, H, H, mat="basalt", seg=500, mesh="wall5", z_off=500)
    for sx, sy in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
        tower(z, cx + sx * H, cy + sy * H, h=4, mat="basalt", roof="pyramid", roof_mat="rust")
        place(z, "pyramid", cx + sx * H, cy + sy * H, scale=(3.5, 3.5, 12.0), mat="basalt", z_off=1600)
    for (dx, dy) in [(0, H), (-H, 0), (H, 0)]:
        tower(z, cx + dx, cy + dy, h=3, mat="basalt", roof="pyramid", roof_mat="rust")
    gx, gy = cx, cy - H
    for sx in (-1, 1):
        tower(z, gx + sx * 900, gy, h=4, mat="basalt", roof="pyramid", roof_mat="rust")
    floor_grid(z, gx, gy, 4, 1, mat="basalt", z_off=1000)
    torch(z, gx - 550, gy - 350, 1.6)
    torch(z, gx + 550, gy - 350, 1.6)
    # 안뜰 + 아성(3층 큰 건물) + 왕좌홀
    floor_grid(z, cx, cy, 16, 16, mat="basalt")
    kx, ky = cx, cy + 1200
    for lvl in range(3):
        rect_walls(z, kx, ky, 1400, 1000, mat="basalt", seg=400, doors={"s": 3} if lvl == 0 else None, z_off=lvl * 400,
                   window_every=3 if lvl > 0 else 0)
    place(z, "pyramid", kx, ky, scale=(30.0, 22.0, 6.0), mat="rust", z_off=1200)
    for sx in (-1, 1):
        place(z, "pyramid", kx + sx * 1400, ky + 1000, scale=(3.0, 3.0, 10.0), mat="basalt", z_off=1200)
    # 왕좌: 단상 + 계단 + 왕좌 + 기둥 열 + 횃불
    place(z, "cube", kx, ky + 500, scale=(5.0, 3.0, 0.8), mat="basalt", z_off=2)
    place(z, "stairs", kx, ky + 350, yaw=-90, scale=(1.5, 1.5, 0.75), mat="basalt")
    th = chair(z, kx, ky + 560, yaw=-90, scale=2.4, fid="Throne_DemonLord")  # 홀 입구(-Y) 를 본다
    th.set_actor_location(unreal.Vector(kx, ky + 560, ground_z(kx, ky + 560) + 82), False, False)
    for i in range(4):
        for sx in (-1, 1):
            place(z, "pillar", kx + sx * 700, ky - 600 + i * 350, scale=(1.2, 1.2, 2.2), mat="basalt")
            emitter(z, FIRE, kx + sx * 700, ky - 600 + i * 350, 1100, 0.9)
    # 결계실(서쪽 별채): 토러스 마법진 + 기둥 5 + 사슬(토러스 작은 것)
    bx, by = cx - 2400, cy - 800
    rect_walls(z, bx, by, 800, 800, mat="basalt", seg=400, doors={"e": 1})
    place(z, "torus", bx, by, scale=(5.0, 5.0, 0.3), mat="gold", z_off=8, collision=False)
    ring(z, bx, by, 380, 5, mesh="pillar", mat="rust", scale=(0.5, 0.5, 0.9))
    for i in range(5):
        ang = 2 * math.pi * i / 5
        place(z, "torus", bx + 380 * math.cos(ang), by + 380 * math.sin(ang), scale=(0.4, 0.4, 0.2), mat="rust", z_off=420, pitch=90,
              collision=False)
    emitter(z, SMOKE, bx, by, 40, 1.2)
    # 안뜰 잡동사니: 우리·창(기둥)·잔해
    hism(z, "cube", [(cx + random.uniform(-3000, 3000), cy + random.uniform(-3000, -400), 0, random.uniform(0, 360), s, s, s * 0.6)
                     for s in [random.uniform(0.4, 1.4) for _ in range(40)]], mat="basalt")
    hism(z, "pillar", [(cx + random.uniform(-3200, 3200), cy + random.uniform(-3200, -600), 0, random.uniform(0, 360), 0.3, 0.3,
                        random.uniform(0.5, 0.9)) for _ in range(30)], mat="rust")
    for (dx, dy) in [(-1500, -2500), (1500, -2500), (-2800, 1500), (2800, 1500)]:
        campfire(z, cx + dx, cy + dy, 1.8)
    trigger("citadel", gx, gy + 700, ext=(1200, 700, 400))
    move_npc("DemonLord", kx, ky + 700, face=(kx, ky - 3000))


def build_roads():
    z = "road"
    road(z, [RUINS, (RUINS[0], RUINS[1] - 1800), GATE_N, (GATE_N[0], GATE_N[1] - 700), PLAZA], width=2)
    road(z, [PLAZA, (PLAZA[0] - 2000, PLAZA[1] - 300), GATE_W, (GATE_W[0] - 900, GATE_W[1]), (LIBRARY[0] + 2200, LIBRARY[1] - 200), LIBRARY])
    road(z, [PLAZA, (1400, -3000), GATE_S, (GATE_S[0], GATE_S[1] - 900), (3500, -6200), (CLEARING[0] - 900, CLEARING[1])])
    road(z, [(CLEARING[0] + 900, CLEARING[1]), (BOMB[0] - 1200, BOMB[1]), (BOMB[0], BOMB[1] + 1200), (BRIDGE[0] - 1700, BRIDGE[1])])
    road(z, [(BRIDGE[0] + 1700, BRIDGE[1]), (OUTPOST[0] - OUT_HW - 900, BRIDGE[1]), (OUTPOST[0] - OUT_HW - 900, OUTPOST[1]),
             (OUTPOST[0] - OUT_HW + 200, OUTPOST[1])])
    road(z, [(OUTPOST[0], OUTPOST[1] + OUT_HW), (OUTPOST[0], OUTPOST[1] + OUT_HW + 2000), (CITADEL[0], CITADEL[1] - CIT_HW - 2200),
             (CITADEL[0], CITADEL[1] - CIT_HW - 300)], width=2)


def build_wilderness():
    """맵 나머지 채우기 — 마을 밖 초원·숲·바위 들판·죽은 숲. 구역/길은 제외."""
    z = "wild"
    ex = [(VIL[0], VIL[1], 6000), (RUINS[0], RUINS[1], 2800), (LIBRARY[0], LIBRARY[1], 2800), (FOREST[0], FOREST[1], FOREST_R + 600),
          (BRIDGE[0], BRIDGE[1], 2400), (OUTPOST[0], OUTPOST[1], OUT_HW + 1200), (CITADEL[0], CITADEL[1], CIT_HW + 1500),
          (RIVER_X, 0, 1200), (RIVER_X, 8000, 1200), (RIVER_X, -8000, 1200), (RIVER_X, 16000, 1200), (RIVER_X, -16000, 1200)]
    forest_patch(z, -17000, -16000, 6500, 420, exclude=ex)
    forest_patch(z, -16500, 14000, 7000, 460, exclude=ex)
    forest_patch(z, 16500, -17000, 6000, 380, exclude=ex)
    forest_patch(z, -5500, 12000, 4500, 220, exclude=ex)
    forest_patch(z, 4500, 8000, 3500, 160, exclude=ex)
    forest_patch(z, 10500, 16500, 4500, 260, dead=True, exclude=ex)  # 마왕성 앞 죽은 숲
    forest_patch(z, 22000, 5000, 3000, 140, dead=True, exclude=ex)
    scatter(z, 0, 0, 24000, 24000, 700, "bush", smin=0.6, smax=1.5, exclude=ex, collision=False)
    scatter(z, 0, 0, 24000, 24000, 220, "rock", smin=0.7, smax=2.4, exclude=ex)
    scatter(z, 20000, 20000, 4500, 4500, 60, "rock", smin=3.0, smax=6.0, exclude=ex, zflat=0.8)  # 북동 바위산
    scatter(z, -21000, 3000, 3500, 6000, 50, "rock", smin=3.0, smax=6.0, exclude=ex, zflat=0.8)  # 서쪽 바위산
    # 이정표(길 갈림)
    for (sx, sy, yaw) in [(GATE_S[0], GATE_S[1] - 1200, 0), (BOMB[0] - 1400, BOMB[1] + 300, 45), (OUTPOST[0], OUTPOST[1] + OUT_HW + 1200, 90)]:
        place(z, "pillar", sx, sy, scale=(0.3, 0.3, 0.45), mat="wood")
        place(z, "cube", sx, sy, yaw=yaw, scale=(0.9, 0.12, 0.25), mat="pine", z_off=200)


def build_env():
    nav = find_actor(cls="NavMeshBoundsVolume")
    if nav:
        nav.set_actor_location(unreal.Vector(0, 0, 400), False, False)
        nav.set_actor_scale3d(unreal.Vector(255, 255, 18))  # 51000×51000×3600
        print("[scene] NavMeshBounds → 전 맵")
    fog = find_actor(cls="ExponentialHeightFog")
    if fog:
        comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if comp:
            comp.set_fog_density(0.02)
            comp.set_fog_inscattering_color(unreal.LinearColor(0.55, 0.5, 0.6, 1.0))
            print("[scene] fog 조정")


def build():
    ensure_hism_bp()
    build_village()
    build_ruins()
    build_library()
    build_forest()
    build_river_bridge()
    build_outpost()
    build_citadel()
    build_roads()
    build_wilderness()
    build_env()


with unreal.ScopedEditorTransaction("Story: 전 맵 빌드"):
    clear_scene()
    build()

print("[scene] actors:", COUNTS, "chairs:", _chair_n)
print("[scene] hism instances:", HISM_TOTAL)
# RecastNavMesh 가 Static 생성이라 에디터에서 빌드한 데이터가 레벨에 저장돼야 PIE 에서 쓴다.
# 비동기 — 몇 초 뒤 is_navigation_being_built 가 False 가 되면 save_current_level 한 번 더.
unreal.SystemLibrary.execute_console_command(WORLD, "RebuildNavigation")
print("[scene] RebuildNavigation 요청 — 완료 후 레벨 재저장 필요")
print("[scene] save:", LES.save_current_level())

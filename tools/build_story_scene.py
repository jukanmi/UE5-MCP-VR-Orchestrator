"""스토리 전 맵 빌더 — Sample 레벨 500m×500m 전체 (2026-09-18 v2). 멱등: 라벨 SCN_* 액터 전부 삭제 후 재생성.

에디터 안에서 실행: MCP `ue_run_python(mode="file", script="<이 파일 절대경로>")` 또는 에디터 Python 콘솔
`py "C:/github/UE5_MCP_VR/tools/build_story_scene.py"`.
구역만 다시: 환경변수 SCN_ONLY="village,citadel" → 그 구역(build_<zone>) 라벨만 지우고 재생성. place_actors 는 안 돈다.
전제: Content/StarterContent (Engine/Samples 에서 복사, Memo 참조) · C++ AStoryZoneTrigger · BP_HISMCluster(이 스크립트가 없으면 생성).

동선(시작→끝):
  ruins(불타는 성 폐허, PlayerStart) → gate(성문) → plaza(광장·제단) → hideout(은신처) → 서문 → library(도서관 폐허)
  → 남문 → forest(숲·약초·폭격 자리) → bridge(강·다리) → outpost(목책 전초기지·포로 우리) → 북문 → 죽은 숲 → citadel(대성채)
서브퀘스트 적 스포너(도적 캠프·다리 오크·죽은 숲 망령) = AEnemySpawner, 적 BP 는 tools/make_enemy_bps.py.
앰비언트 주민 12명(광장·시장·성문 안·은신처·도서관) = AVillagerCharacter 정적 배치, 주민 BP 는 tools/make_villager_bps.py.
구조물 = StaticMeshActor, 식생·바위·잔해 = BP_HISMCluster(HISM). 손으로 옮긴 소품은 재실행 시 초기화 — 좌표는 여기서 고친다.
"""

import math
import os
import random

import unreal

random.seed(11)
ONLY = [z for z in os.environ.get("SCN_ONLY", "").split(",") if z]

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
    a = EAS.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(x, y, z), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    )
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
        place(
            zone,
            "rock",
            x + 90 * math.cos(ang),
            y + 90 * math.sin(ang),
            yaw=random.uniform(0, 360),
            scale=(0.25, 0.25, 0.15),
        )
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
    for old in EAS.get_all_level_actors():  # 라벨이 SCN_<zone>_ 밖이라 구역 단독 재빌드 때 여기서 교체
        if old.get_actor_label() == f"SCN_{zone}_trigger":
            EAS.destroy_actor(old)
    a = EAS.spawn_actor_from_class(
        unreal.StoryZoneTrigger, unreal.Vector(x, y, ground_z(x, y) + 100), unreal.Rotator(0, 0, 0)
    )
    a.set_editor_property("ZoneName", zone)
    a.get_editor_property("Box").set_box_extent(unreal.Vector(*ext))
    return _finish(a, f"SCN_{zone}_trigger")


def wall_line(zone, x0, y0, x1, y1, seg=400.0, mesh="wall", mat="stone", door_at=(), window_every=0, z_off=0.0):
    """(x0,y0)→(x1,y1) 직선 벽. Wall_400x400 피벗은 시작 모서리라 세그먼트가 이어 붙는다. door_at: 문 세그먼트 인덱스들."""
    dx, dy = x1 - x0, y1 - y0
    n = max(1, int(round(math.hypot(dx, dy) / seg)))
    step = math.hypot(dx, dy) / n  # 실제 간격(길이가 seg 배수가 아니면 seg 와 다르다)
    yaw = math.degrees(math.atan2(dy, dx))
    for i in range(n):
        t = i / n
        key = "door" if i in door_at else ("window" if window_every and i % window_every == window_every - 1 else mesh)
        # 메시 길이(문·창 400, wall5 500)를 간격에 맞춰 늘려 틈 없앰. 문·창은 높이도 seg(층 높이)에 맞춤 — 500 벽에 400 문이면 위 100 구멍
        sc = (step / (500 if key == "wall5" else 400), 1, seg / 400 if key in ("door", "window") else 1)
        place(zone, key, x0 + dx * t, y0 + dy * t, yaw=yaw, mat=mat, z_off=z_off, scale=sc)


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
            place(
                zone,
                "floor",
                cx + (i - (nx - 1) / 2) * 400 - 200,
                cy + (j - (ny - 1) / 2) * 400 - 200,
                mat=mat,
                z_off=z_off,
            )


def ring(zone, cx, cy, r, n, mesh="pillar", mat="stone", scale=(1, 1, 1), tilt=0.0, z_off=0.0):
    for i in range(n):
        ang = 2 * math.pi * i / n
        place(
            zone,
            mesh,
            cx + r * math.cos(ang),
            cy + r * math.sin(ang),
            yaw=math.degrees(ang),
            scale=scale,
            mat=mat,
            roll=random.uniform(-tilt, tilt),
            pitch=random.uniform(-tilt, tilt),
            z_off=z_off,
        )


def tower(zone, x, y, h=2, mat="hewn", roof="pyramid", roof_mat="basalt", fire_top=True, z0=0.0, doors=None):
    """망루/성탑: 400 각 기둥형 사각 벽 h 층 + 지붕. z0 = 바닥 높이(성벽 위 소탑), doors = 첫 층 문(통로 관통용)."""
    for lvl in range(h):
        rect_walls(zone, x, y, 200, 200, mat=mat, z_off=z0 + lvl * 400, doors=doors if lvl == 0 else None)
    place(zone, roof, x, y, scale=(4.6, 4.6, 3.0), mat=roof_mat, z_off=z0 + h * 400)
    if fire_top:
        emitter(zone, FIRE, x, y, z0 + h * 400 + 300, 0.8)


def thick_walls(zone, cx, cy, hw, hh, doors, mat, seg=400.0, mesh="wall", depth=400.0, turrets=()):
    """두꺼운 성벽 2층: 바깥 벽 + depth 안쪽 벽 + 위 통로 바닥 + 바깥 흉벽·총안 + 문 통로 측벽(빈 벽 속 가림).
    doors 는 rect_walls 와 같은 {"n"|"s"|"e"|"w": 인덱스}. turrets = 통로 위 소탑 중심들 — 그 자리 흉벽은 비운다.
    바닥·흉벽·총안은 HISM 3 액터. 통로 바닥 z(= 2*seg) 를 돌려준다."""
    top = 2 * seg
    sides = {
        "s": ((cx - hw, cy - hh), (cx + hw, cy - hh)),
        "e": ((cx + hw, cy - hh), (cx + hw, cy + hh)),
        "n": ((cx + hw, cy + hh), (cx - hw, cy + hh)),
        "w": ((cx - hw, cy + hh), (cx - hw, cy - hh)),
    }
    floors, parapet, merlons = [], [], []

    def free(x, y):
        return all(abs(x - tx) > 210 or abs(y - ty) > 210 for tx, ty in turrets)

    for side, ((x0, y0), (x1, y1)) in sides.items():
        length = math.hypot(x1 - x0, y1 - y0)
        ux, uy = (x1 - x0) / length, (y1 - y0) / length
        nx, ny = -uy, ux  # 안쪽 법선
        yaw = math.degrees(math.atan2(uy, ux))
        di = doors.get(side)
        for lvl in (0, 1):
            d = (di,) if lvl == 0 else ()
            wall_line(zone, x0, y0, x1, y1, seg, mesh, mat, d, 0, lvl * seg)
            wall_line(
                zone,
                x0 + nx * depth,
                y0 + ny * depth,
                x1 + nx * depth,
                y1 + ny * depth,
                seg,
                mesh,
                mat,
                d,
                0,
                lvl * seg,
            )
        step = length / max(1, round(length / seg))  # wall_line 과 같은 실제 세그먼트 간격
        if di is not None:
            for k in (0, 1):
                px, py = x0 + ux * step * (di + k), y0 + uy * step * (di + k)
                place(zone, "wall", px, py, yaw=yaw + 90, scale=(depth / 400, 1, top / 400), mat=mat)
        # 통로 바닥(400 타일): 모서리 정사각형은 남·북 띠가 덮으니 동·서는 양 끝 하나씩 뺀다. z +1 = 벽 윗면과 z-fighting 방지
        lo, hi = (0, int(length / 400)) if side in ("s", "n") else (1, int(length / 400) - 1)
        floors += [(x0 + ux * 400 * i, y0 + uy * 400 * i, top + 1, yaw, 1, 1, 1) for i in range(lo, hi)]
        # 바깥 흉벽 60 + 그 위 총안 요철 60(100 마다 교대)
        for i in range(round(length / step)):
            px, py = x0 + ux * step * (i + 0.5), y0 + uy * step * (i + 0.5)
            if free(px, py):
                parapet.append((px, py, top, yaw, step / 100, 0.2, 0.6))
        for j in range(int(length / 200)):
            px, py = x0 + ux * (200 * j + 100), y0 + uy * (200 * j + 100)
            if free(px, py):
                merlons.append((px, py, top + 60, yaw, 1, 0.2, 0.6))
    hism(zone, "floor", floors, mat=mat)
    hism(zone, "cube", parapet, mat=mat)
    hism(zone, "cube", merlons, mat=mat)
    return top


def stair(zone, x, y, yaw, rise, mat, width=200.0, tread=40.0, landing=200.0):
    """돌계단(HISM 1 액터). (x, y) = 꼭대기 끝(통로에 닿는 자리), yaw = 오르는 방향. 그 뒤로 참(landing) + 단이 내려간다.
    단 높이 ≤ 32(NavMesh 계단 한계 35). 꼭대기 참은 에이전트 반지름(35)보다 넓어야 통로 NavMesh 와 이어진다(실측: 단 하나론 끊김).
    tread 정육면체 블록으로 속까지 채운다 — 긴 기둥 하나로 하면 UV 가 늘어나 텍스처가 세로로 번진다(실측)."""
    n = math.ceil(rise / 32)
    riser = rise / n
    nl = round(landing / tread)
    r = math.radians(yaw)
    ux, uy, vx, vy = math.cos(r), math.sin(r), -math.sin(r), math.cos(r)  # 오르는 방향, 폭 방향
    nw = max(1, round(width / tread))
    blocks = [
        (
            x - ux * tread * (m + 0.5) + vx * tread * (j - (nw - 1) / 2),
            y - uy * tread * (m + 0.5) + vy * tread * (j - (nw - 1) / 2),
            riser * lvl,
            yaw,
            tread / 100,
            tread / 100,
            riser / 100,
        )
        for m in range(nl + n)
        for lvl in range(n - max(0, m - nl))
        for j in range(nw)
    ]
    hism(zone, "cube", blocks, mat=mat)


def house(zone, x, y, yaw=0.0, w=1, d=1, mat="brick", roof_mat="walnut", door_side="s", window=True):
    """집: (w×400)×(d×400) 평면, 4면 벽, 문 1, 창, 쐐기 지붕. yaw 는 집 전체 회전(0/90/180/270 권장)."""
    hw, hh = w * 200, d * 200
    rad = math.radians(yaw)

    def R(px, py):
        return x + px * math.cos(rad) - py * math.sin(rad), y + px * math.sin(rad) + py * math.cos(rad)

    floor_grid(zone, x, y, w, d, mat="pine", z_off=1.0)
    sides = {
        "s": ((-hw, -hh), (hw, -hh)),
        "e": ((hw, -hh), (hw, hh)),
        "n": ((hw, hh), (-hw, hh)),
        "w": ((-hw, hh), (-hw, -hh)),
    }
    for side, ((ax, ay), (bx, by)) in sides.items():
        n = max(1, int(round(math.hypot(bx - ax, by - ay) / 400)))
        for i in range(n):
            t = i / n
            px, py = ax + (bx - ax) * t, ay + (by - ay) * t
            wx, wy = R(px, py)
            wyaw = math.degrees(math.atan2(by - ay, bx - ax)) + yaw
            key = (
                "door"
                if (side == door_side and i == n // 2)
                else ("window" if window and i == 0 and side != door_side else "wall")
            )
            place(zone, key, wx, wy, yaw=wyaw, mat=mat)
    # 지붕: 우진각 — Shape_QuadPyramid(100 각, 피벗 바닥 중심) 처마 15% 돌출. Wedge_A 는 위에서 본 삼각 기둥이라 지붕 불가.
    rx, ry = R(0, 0)
    place(
        zone,
        "pyramid",
        rx,
        ry,
        yaw=yaw,
        scale=(hw / 50 * 1.15, hh / 50 * 1.15, 1.6 + 0.4 * max(w, d)),
        mat=roof_mat,
        z_off=398,
    )


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
    handle, _ = sds.add_new_subobject(
        unreal.AddNewSubobjectParams(
            parent_handle=root, new_class=unreal.HierarchicalInstancedStaticMeshComponent, blueprint_context=bp
        )
    )
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
    # Static 자식은 Static 부모에만 붙음 — DefaultSceneRoot 도 함께 Static
    a.root_component.set_mobility(unreal.ComponentMobility.STATIC)
    h.set_mobility(unreal.ComponentMobility.STATIC)
    xs = [
        unreal.Transform(unreal.Vector(x, y, ground_z(x, y) + zo), unreal.Rotator(yaw=yaw), unreal.Vector(sx, sy, sz))
        for (x, y, zo, yaw, sx, sy, sz) in transforms
    ]
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
                crowns.append(
                    (x, y, h * 100 * 0.78, random.uniform(0, 360), cw * 0.7, cw * 0.7, random.uniform(2.2, 3.0))
                )
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
        # 수관은 시각용 — 충돌 있으면 밑동 높이(1.3~2.3m)에서 캐릭터 캡슐(1.76m)이 걸려 AI 가 끼인다(실측)
        hism(zone, "cone", crowns, mat="grass", collision=False)
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
        lb = a.get_actor_label()
        if lb.startswith("SCN_") and (not ONLY or any(lb.startswith(f"SCN_{z}_") for z in ONLY)):
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
    # 기존 액터는 modify() 없이 옮기면 외부 액터 패키지가 dirty 안 돼 저장에서 빠진다(실측)
    a.modify(True)
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
VIL = (-1000.0, -2000.0)  # 마을 중심
VIL_HW, VIL_HH = 4200.0, 2600.0  # 마을 성벽 반폭/반높이 → 84m × 52m
GATE_N = (VIL[0], VIL[1] + VIL_HH)  # 북문 (-1000, 600)
GATE_W = (VIL[0] - VIL_HW, VIL[1])  # 서문 (-5200, -2000)
GATE_S = (VIL[0] + 1400, VIL[1] - VIL_HH)  # 남문 (400, -4600)
PLAZA = (VIL[0], VIL[1] + 200)  # 광장 (-1000, -1800)
HIDEOUT = (VIL[0] - 2600, VIL[1] - 1400)  # 은신처 (-3600, -3400)
RUINS = (-1000.0, 6500.0)  # 불타는 성 폐허(시작)
LIBRARY = (-10500.0, -3200.0)
FOREST = (7500.0, -7500.0)
FOREST_R = 5200.0
CLEARING = (7000.0, -6800.0)  # 약초 빈터
BOMB = (11000.0, -4200.0)  # 폭격 자리(Skadi)
RIVER_X = 13800.0  # 강 중심선 x, 남북
BRIDGE = (RIVER_X, -1500.0)
OUTPOST = (19000.0, -500.0)  # 서벽 x=16800 — 다리(12400..15200) 와 안 겹치게
OUT_HW = 2200.0
CITADEL = (17500.0, 12500.0)
CIT_HW = 3800.0
# 서브퀘스트 적 스포너(시나리오 §4). 적 BP 는 /Game/Blueprint/Enemy (tools/make_enemy_bps.py 가 생성).
ENEMY_BP = "/Game/Blueprint/Enemy"
VILLAGER_BP = "/Game/Blueprint/Villager"
BANDIT_CAMP = (4300.0, -5600.0)  # 남쪽 숲길 외곽 — s_hunt_forest_raiders (남문 3.9km·빈터 3.1km)
ORC_LAIR = (BRIDGE[0] + 600.0, BRIDGE[1] - 800.0)  # 다리 동쪽 교각 밑 — s_hunt_bridge_troll
DEAD_FOREST = (11500.0, 15500.0)  # 마왕성 앞 죽은 숲 — s_hunt_dead_wraith


def build_village():
    z = "village"
    # 성벽(두께 4.2m, 위 통로) + 모서리·성문루 소탑(통로 관통 문) + 문 3 (북=정문, 서, 남)
    cx, cy = VIL
    hw, hh = VIL_HW, VIL_HH
    gx, gy = GATE_N
    doors = {"n": int(2 * hw / 400) // 2, "w": int(2 * hh / 400) // 2, "s": int((GATE_S[0] - (cx - hw)) / 400)}
    corners = [(cx + sx * (hw - 200), cy + sy * (hh - 200), sx, sy) for sx, sy in [(-1, -1), (1, -1), (1, 1), (-1, 1)]]
    gate_turrets = [(gx - 400, gy - 200), (gx + 400, gy - 200)]  # 문 세그먼트 바로 양옆
    top = thick_walls(z, cx, cy, hw, hh, doors, "hewn", turrets=[(x, y) for x, y, _, _ in corners] + gate_turrets)
    for x, y, sx, sy in corners:  # 통로가 꺾이는 두 면에 문
        tower(z, x, y, h=1, z0=top, doors={"s" if sy > 0 else "n": 0, "w" if sx > 0 else "e": 0})
    for x, y in gate_turrets:
        tower(z, x, y, h=1, z0=top, doors={"e": 0, "w": 0})
    # 계단(안쪽 벽면 따라, 꼭대기 참이 소탑·문 옆 통로에 닿게): 북문 양옆 2 + 서문(문 북쪽)·남문(문 동쪽) 1
    stair(z, gx + 600, gy - 510, 180, top, "hewn")
    stair(z, gx - 600, gy - 510, 0, top, "hewn")
    stair(z, GATE_W[0] + 510, GATE_W[1] + 200, -90, top, "hewn")
    stair(z, GATE_S[0] + 400, GATE_S[1] + 510, 180, top, "hewn")
    # 문 횃불 (벽 안쪽 면에서 200 이상 띄움, 계단 피해서)
    torch(z, gx - 420, gy - 600)
    torch(z, gx + 420, gy - 600)
    torch(z, GATE_W[0] + 600, GATE_W[1] - 300)
    torch(z, GATE_S[0] - 300, GATE_S[1] + 600)
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
    place(z, "pyramid", wx, wy, scale=(2.4, 2.4, 1.0), mat="walnut", z_off=250)
    trigger("plaza", px, py, ext=(1000, 1000, 250))
    # 집들 (광장·길·은신처·성벽 안쪽 4.2m 띠·계단 피해서)
    houses = [
        (-3200, -900, 0, 2, 1),
        (-3200, -300, 0, 1, 1),
        (800, -300, 180, 2, 1),
        (1600, -800, 90, 1, 2),
        (2300, -2600, 90, 1, 2),
        (-2400, -3600, 0, 2, 1),
        (500, -3700, 0, 1, 1),
        (2500, -350, 180, 1, 1),
        (-1000, -3900, 0, 2, 1),
        (1900, -3700, 180, 1, 1),
    ]
    for hx, hy, yaw, w, d in houses:
        house(
            z,
            hx,
            hy,
            yaw=yaw,
            w=w,
            d=d,
            mat=random.choice(["brick", "wall", "cut"]),
            roof_mat=random.choice(["walnut", "pine", "basalt"]),
            door_side="s",
        )
    # 시장: 가판대(테이블+천장 쐐기) + 상자·통
    for i, (mx, my) in enumerate([(-200, -3000), (400, -3000), (1000, -3000)]):
        place(z, "table", mx, my, mat="wood")
        place(z, "pillar", mx - 150, my + 150, scale=(0.3, 0.3, 0.5), mat="wood")
        place(z, "pillar", mx + 150, my + 150, scale=(0.3, 0.3, 0.5), mat="wood")
        place(
            z,
            "cube",
            mx,
            my,
            yaw=0,
            scale=(3.6, 2.2, 0.08),
            mat=random.choice(["rust", "gold", "walnut"]),
            z_off=240,
            pitch=0,
            roll=-12,
        )
    crates = [
        (mx + random.uniform(-500, 500), my + random.uniform(-200, 500), 0, random.uniform(0, 360), s, s, s)
        for (mx, my) in [(-3000, -2200), (1900, -1500), (-600, -3300)]
        for s in [random.uniform(0.5, 0.9) for _ in range(4)]
    ]
    hism(z, "cube", crates, mat="wood")
    barrels = [
        (bx + random.uniform(-300, 300), by + random.uniform(-300, 300), 0, 0, 0.5, 0.5, 0.8)
        for (bx, by) in [(-2800, -1900), (2200, -1300), (900, -3500)]
        for _ in range(3)
    ]
    hism(z, "cyl", barrels, mat="walnut")
    # 가로등 횃불
    for tx, ty in [(-2000, -1000), (600, -1000), (-2000, -3000), (1700, -300), (-3600, -2000), (2600, -3600)]:
        torch(z, tx, ty)
    # 은신처: 2×2 건물 + 작업대·서가·침대
    hx, hy = HIDEOUT
    house(z, hx, hy, yaw=0, w=2, d=2, mat="cut", roof_mat="basalt", door_side="n", window=False)
    place(z, "table", hx, hy - 150, mat="wood")
    place(z, "shelf", hx - 300, hy - 300, yaw=90, mat="wood")
    place(z, "shelf", hx + 300, hy - 300, yaw=-90, mat="wood")
    place(z, "lamp", hx + 380, hy + 100, yaw=180, z_off=220)
    trigger("hideout", hx, hy, ext=(420, 420, 250))
    # 성문 트리거 (문 바로 안쪽)
    trigger("gate", gx, gy - 700, ext=(800, 300, 250))


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
    for dx, dy, yaw, pitch in [(-800, 300, 20, -35), (700, -500, -30, 40), (300, 800, 60, -50)]:
        place(z, "wall3", rx + dx, ry + dy, yaw=yaw, pitch=pitch, mat="hewn")
    tower(z, rx - 1600, ry + 1200, h=2, fire_top=True)
    tower(z, rx + 1600, ry + 1200, h=1, fire_top=True)
    # 불·연기
    for dx, dy, s in [(-500, 200, 2.2), (600, 500, 1.8), (200, -700, 2.5), (-1100, -300, 1.6), (1000, -200, 2.0)]:
        emitter(z, FIRE, rx + dx, ry + dy, 10, s)
        emitter(z, SMOKE, rx + dx, ry + dy, 200, s)
    debris = [
        (rx + random.uniform(-1500, 1500), ry + random.uniform(-1100, 1100), 0, random.uniform(0, 360), s, s, s * 0.6)
        for s in [random.uniform(0.4, 1.3) for _ in range(40)]
    ]
    hism(z, "cube", debris, mat="basalt")
    hism(
        z,
        "rock",
        [
            (rx + random.uniform(-1700, 1700), ry + random.uniform(-1300, 1300), 0, random.uniform(0, 360), s, s, s)
            for s in [random.uniform(0.6, 1.5) for _ in range(14)]
        ],
    )
    # 왕좌홀 흔적: 제단 + 부서진 조각상
    place(z, "cube", rx, ry, scale=(2.0, 2.0, 0.3), mat="basalt", z_off=2)
    place(z, "statue", rx, ry, z_off=32, scale=(1.3, 1.3, 1.3), mat="rust", pitch=25)
    # PlayerStart(ry+600) 가 박스 안에 있으면 스폰 시 BeginOverlap 이 안 뜬다(실측) — 남쪽 절반만 덮어 걸어 나가며 밟게
    trigger("ruins", rx, ry - 500, ext=(1600, 500, 300))


def build_library():
    z = "library"
    lx, ly = LIBRARY
    floor_grid(z, lx, ly, 6, 4, mat="stone")
    # 열주 2열 + 부서진 지붕 조각 + 서가 회랑
    for i in range(6):
        for sy in (-1, 1):
            place(
                z,
                "pillar",
                lx - 1000 + i * 400,
                ly + sy * 600,
                mat="stone",
                scale=(1.2, 1.2, 1.4),
                roll=random.uniform(-10, 10) if random.random() < 0.4 else 0,
            )
    wall_line(z, lx - 1300, ly + 900, lx + 1300, ly + 900, mat="brick", mesh="wall3", window_every=2)
    wall_line(z, lx - 1300, ly - 900, lx - 500, ly - 900, mat="brick", mesh="wall3")
    wall_line(z, lx + 400, ly - 900, lx + 1300, ly - 900, mat="brick", mesh="wall3")
    wall_line(z, lx - 1300, ly + 900, lx - 1300, ly - 900, mat="brick", mesh="wall3", window_every=2)
    for dx, dy, yaw, roll in [
        (-300, 550, 0, -8),
        (100, 550, 0, 0),
        (500, 550, 0, 6),
        (-1250, 200, 90, 0),
        (-1250, -200, 90, -5),
        (900, 550, 0, 0),
        (-700, 550, 0, 0),
    ]:
        place(z, "shelf", lx + dx, ly + dy, yaw=yaw, roll=roll, mat="wood")
    for dx, dy, yaw, pitch in [(300, -300, 30, -40), (-900, 100, -20, 30)]:
        place(z, "wall3", lx + dx, ly + dy, yaw=yaw, pitch=pitch, mat="brick")
    place(z, "statue", lx, ly - 400, scale=(1.5, 1.5, 1.5), mat="stone")
    chair(z, lx + 60, ly + 80, yaw=-90, fid="Chair_Library_Reading")  # 탁자(-Y) 를 본다
    place(z, "table", lx + 60, ly - 120, mat="wood")
    hism(
        z,
        "cube",
        [
            (
                lx + random.uniform(-1200, 1200),
                ly + random.uniform(-800, 800),
                0,
                random.uniform(0, 360),
                0.25,
                0.35,
                0.08,
            )
            for _ in range(60)
        ],
        mat="pine",
    )  # 흩어진 책
    hism(
        z,
        "rock",
        [
            (lx + random.uniform(-1800, 1800), ly + random.uniform(-1400, 1400), 0, random.uniform(0, 360), s, s, s)
            for s in [random.uniform(0.5, 1.4) for _ in range(12)]
        ],
    )
    torch(z, lx - 1300, ly - 1100)
    torch(z, lx + 1300, ly - 1100)
    trigger("library", lx, ly, ext=(1400, 1000, 300))


def build_forest():
    z = "forest"
    fx, fy = FOREST
    cxp, cyp = CLEARING
    bx, by = BOMB
    forest_patch(
        z, fx, fy, FOREST_R, 520, exclude=[(cxp, cyp, 900), (bx, by, 900), (GATE_S[0] + 2500, GATE_S[1] - 1500, 900)]
    )
    # 약초 빈터: 바위 원 + 약초(초록 덤불) + 드롭
    ring(z, cxp, cyp, 700, 7, mesh="rock", scale=(1.0, 1.0, 0.7))
    hism(
        z,
        "bush",
        [
            (cxp + random.uniform(-500, 500), cyp + random.uniform(-500, 500), 0, random.uniform(0, 360), 0.5, 0.5, 0.5)
            for _ in range(16)
        ],
        collision=False,
    )
    drop_cls = unreal.EditorAssetLibrary.load_blueprint_class("/Game/Blueprint/Entity/BP_DropItem")
    d = EAS.spawn_actor_from_class(drop_cls, unreal.Vector(cxp, cyp, ground_z(cxp, cyp) + 20), unreal.Rotator(0, 0, 0))
    item = d.get_editor_property("ItemData")
    item.set_editor_property("item_template_id", "HerbBasket")
    d.set_editor_property("ItemData", item)
    _finish(d, "SCN_forest_HerbBasket")
    campfire(z, cxp + 350, cyp - 300, 1.0)
    trigger("forest", cxp, cyp, ext=(1200, 1200, 300))
    # 폭격 자리: 분화구(토러스) + 연기 + 잔해 + 부서진 마왕군 수레
    for dx, dy, s in [(0, 0, 3.0), (600, 400, 2.0), (-500, 500, 2.4)]:
        place(z, "torus", bx + dx, by + dy, scale=(s, s, 0.25), mat="basalt", z_off=0, collision=False)
        emitter(z, SMOKE, bx + dx, by + dy, 30, 1.5)
    emitter(z, FIRE, bx + 600, by + 400, 10, 1.2)
    hism(
        z,
        "cube",
        [
            (bx + random.uniform(-900, 900), by + random.uniform(-900, 900), 0, random.uniform(0, 360), s, s, s * 0.5)
            for s in [random.uniform(0.3, 1.0) for _ in range(30)]
        ],
        mat="basalt",
    )
    place(z, "cube", bx + 900, by - 300, yaw=30, scale=(2.0, 1.2, 0.8), mat="walnut", roll=25)  # 뒤집힌 수레
    place(z, "torus", bx + 700, by - 200, yaw=90, scale=(0.8, 0.8, 0.2), mat="rust", pitch=90)
    place(z, "rock", bx - 700, by - 600, scale=(2.4, 2.4, 1.3))  # Skadi 가 올라선 바위


def build_river_bridge():
    z = "bridge"
    # 강: 남북 긴 수면 (충돌 없음) + 강둑 바위
    for i in range(-6, 7):
        place(z, "plane", RIVER_X, i * 4000, scale=(9.0, 40.0, 1.0), mat="water", z_off=4, collision=False)
    scatter(
        z, RIVER_X, 0, 900, 24000, 260, "rock", smin=0.4, smax=1.3, exclude=[(BRIDGE[0], BRIDGE[1], 1400)], zflat=0.4
    )
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
        tower(z, ox + sx * OUT_HW, oy + sy * OUT_HW, h=2, mat="walnut", roof="pyramid", roof_mat="rust")
    torch(z, ox - OUT_HW - 200, oy - 600)
    torch(z, ox - OUT_HW - 200, oy + 600)
    floor_grid(z, ox, oy, 4, 4, mat="pebble")
    # 막사 2 + 지휘 천막 + 무기대·상자·모닥불
    house(z, ox + 1200, oy + 1200, yaw=270, w=2, d=1, mat="walnut", roof_mat="rust", door_side="s")
    house(z, ox + 1200, oy - 1200, yaw=270, w=2, d=1, mat="walnut", roof_mat="rust", door_side="s")
    place(z, "pyramid", ox - 900, oy + 1100, scale=(9.0, 9.0, 3.5), mat="rust")  # 천막
    place(z, "table", ox, oy, mat="wood")
    campfire(z, ox - 500, oy - 900, 1.6)
    hism(
        z,
        "cube",
        [
            (ox + random.uniform(-1800, 1800), oy + random.uniform(-1800, 1800), 0, random.uniform(0, 360), s, s, s)
            for s in [random.uniform(0.4, 0.9) for _ in range(18)]
        ],
        mat="wood",
    )
    hism(
        z,
        "cyl",
        [(ox + random.uniform(-1800, 1800), oy + random.uniform(-1800, 1800), 0, 0, 0.5, 0.5, 0.8) for _ in range(8)],
        mat="rust",
    )
    # 마왕군 깃발: 기둥 + 검은 판
    for fx_, fy_ in [(ox - OUT_HW + 500, oy - 900), (ox - OUT_HW + 500, oy + 900), (ox, oy + 1900)]:
        place(z, "pillar", fx_, fy_, scale=(0.35, 0.35, 1.3), mat="rust")
        place(z, "cube", fx_ + 60, fy_, scale=(0.06, 1.0, 1.4), mat="basalt", z_off=480, collision=False)
    # 포로 우리 (Elara)
    cx, cy = ox + 900, oy + 300
    ring(z, cx, cy, 300, 8, mesh="pillar", mat="rust", scale=(0.5, 0.5, 0.8))
    place(z, "torus", cx, cy, scale=(3.4, 3.4, 0.2), mat="rust", z_off=390, collision=False)
    trigger("outpost", ox - OUT_HW + 400, oy, ext=(500, 900, 300))  # 서문 안쪽


def build_citadel():
    z = "citadel"
    cx, cy = CITADEL
    H = CIT_HW
    # 단상(넓은 검은 대지) + 외벽 2층(두께 4.2m, 위 통로) + 성문루(남) + 모서리·중간 소탑 + 성문 안쪽 계단 2
    place(z, "cube", cx, cy, scale=(2 * H / 100 + 6, 2 * H / 100 + 6, 0.5), mat="basalt", z_off=-45)
    gx, gy = cx, cy - H
    door = int(2 * H / 500) // 2
    corners = [(cx + sx * (H - 200), cy + sy * (H - 200), sx, sy) for sx, sy in [(-1, -1), (1, -1), (1, 1), (-1, 1)]]
    mids = [
        (cx, cy + H - 200, {"e": 0, "w": 0}),
        (cx - H + 200, cy, {"n": 0, "s": 0}),
        (cx + H - 200, cy, {"n": 0, "s": 0}),
    ]
    step = 2 * H / round(2 * H / 500)  # wall_line 실제 세그먼트 간격
    gate_x = [cx - H + step * (door - 0.5), cx - H + step * (door + 1.5)]  # 문 세그먼트 양옆 슬롯 중심
    turrets = [(x, y) for x, y, _, _ in corners] + [(x, y) for x, y, _ in mids] + [(x, gy + 200) for x in gate_x]
    top = thick_walls(z, cx, cy, H, H, {"s": door}, "basalt", seg=500, mesh="wall5", turrets=turrets)
    for x, y, sx, sy in corners:
        tower(
            z,
            x,
            y,
            h=2,
            mat="basalt",
            roof_mat="rust",
            z0=top,
            doors={"s" if sy > 0 else "n": 0, "w" if sx > 0 else "e": 0},
        )
        place(z, "pyramid", x, y, scale=(3.5, 3.5, 12.0), mat="basalt", z_off=top + 800)
    for x, y, d in mids:
        tower(z, x, y, h=1, mat="basalt", roof_mat="rust", z0=top, doors=d)
    for x in gate_x:
        tower(z, x, gy + 200, h=2, mat="basalt", roof_mat="rust", z0=top, doors={"e": 0, "w": 0})
    stair(z, gate_x[1] + 200, gy + 510, 180, top, "basalt")
    stair(z, gate_x[0] - 200, gy + 510, 0, top, "basalt")
    torch(z, gx - 550, gy - 350, 1.6)
    torch(z, gx + 550, gy - 350, 1.6)
    # 안뜰 + 아성(3층 큰 건물) + 왕좌홀
    floor_grid(z, cx, cy, 16, 16, mat="basalt")
    kx, ky = cx, cy + 1200
    for lvl in range(3):
        rect_walls(
            z,
            kx,
            ky,
            1400,
            1000,
            mat="basalt",
            seg=400,
            doors={"s": 3} if lvl == 0 else None,
            z_off=lvl * 400,
            window_every=3 if lvl > 0 else 0,
        )
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
        place(
            z,
            "torus",
            bx + 380 * math.cos(ang),
            by + 380 * math.sin(ang),
            scale=(0.4, 0.4, 0.2),
            mat="rust",
            z_off=420,
            pitch=90,
            collision=False,
        )
    emitter(z, SMOKE, bx, by, 40, 1.2)
    # 안뜰 잡동사니: 우리·창(기둥)·잔해
    hism(
        z,
        "cube",
        [
            (
                cx + random.uniform(-3000, 3000),
                cy + random.uniform(-3000, -400),
                0,
                random.uniform(0, 360),
                s,
                s,
                s * 0.6,
            )
            for s in [random.uniform(0.4, 1.4) for _ in range(40)]
        ],
        mat="basalt",
    )
    hism(
        z,
        "pillar",
        [
            (
                cx + random.uniform(-3200, 3200),
                cy + random.uniform(-3200, -600),
                0,
                random.uniform(0, 360),
                0.3,
                0.3,
                random.uniform(0.5, 0.9),
            )
            for _ in range(30)
        ],
        mat="rust",
    )
    for dx, dy in [(-1500, -2500), (1500, -2500), (-2800, 1500), (2800, 1500)]:
        campfire(z, cx + dx, cy + dy, 1.8)
    trigger("citadel", gx, gy + 700, ext=(1200, 700, 400))


def build_roads():
    z = "road"
    road(z, [RUINS, (RUINS[0], RUINS[1] - 1800), GATE_N, (GATE_N[0], GATE_N[1] - 700), PLAZA], width=2)
    road(
        z,
        [
            PLAZA,
            (PLAZA[0] - 2000, PLAZA[1] - 300),
            GATE_W,
            (GATE_W[0] - 900, GATE_W[1]),
            (LIBRARY[0] + 2200, LIBRARY[1] - 200),
            LIBRARY,
        ],
    )
    road(
        z, [PLAZA, (1400, -3000), GATE_S, (GATE_S[0], GATE_S[1] - 900), (3500, -6200), (CLEARING[0] - 900, CLEARING[1])]
    )
    road(
        z,
        [
            (CLEARING[0] + 900, CLEARING[1]),
            (BOMB[0] - 1200, BOMB[1]),
            (BOMB[0], BOMB[1] + 1200),
            (BRIDGE[0] - 1700, BRIDGE[1]),
        ],
    )
    road(
        z,
        [
            (BRIDGE[0] + 1700, BRIDGE[1]),
            (OUTPOST[0] - OUT_HW - 900, BRIDGE[1]),
            (OUTPOST[0] - OUT_HW - 900, OUTPOST[1]),
            (OUTPOST[0] - OUT_HW + 200, OUTPOST[1]),
        ],
    )
    road(
        z,
        [
            (OUTPOST[0], OUTPOST[1] + OUT_HW),
            (OUTPOST[0], OUTPOST[1] + OUT_HW + 2000),
            (CITADEL[0], CITADEL[1] - CIT_HW - 2200),
            (CITADEL[0], CITADEL[1] - CIT_HW - 300),
        ],
        width=2,
    )


def build_wilderness():
    """맵 나머지 채우기 — 마을 밖 초원·숲·바위 들판·죽은 숲. 구역/길은 제외."""
    z = "wild"
    ex = [
        (VIL[0], VIL[1], 6000),
        (RUINS[0], RUINS[1], 2800),
        (LIBRARY[0], LIBRARY[1], 2800),
        (FOREST[0], FOREST[1], FOREST_R + 600),
        (BRIDGE[0], BRIDGE[1], 2400),
        (OUTPOST[0], OUTPOST[1], OUT_HW + 1200),
        (CITADEL[0], CITADEL[1], CIT_HW + 1500),
        (RIVER_X, 0, 1200),
        (RIVER_X, 8000, 1200),
        (RIVER_X, -8000, 1200),
        (RIVER_X, 16000, 1200),
        (RIVER_X, -16000, 1200),
    ]
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
    for sx, sy, yaw in [
        (GATE_S[0], GATE_S[1] - 1200, 0),
        (BOMB[0] - 1400, BOMB[1] + 300, 45),
        (OUTPOST[0], OUTPOST[1] + OUT_HW + 1200, 90),
    ]:
        place(z, "pillar", sx, sy, scale=(0.3, 0.3, 0.45), mat="wood")
        place(z, "cube", sx, sy, yaw=yaw, scale=(0.9, 0.12, 0.25), mat="pine", z_off=200)


def place_actors():
    """기존 액터(NPC·PlayerStart·Bed) 스토리 위치 재배치. 전체 재빌드 없이 단독 호출 가능 —
    파일을 모듈 레벨 `with unreal.ScopedEditorTransaction` 줄에서 잘라 앞부분만 exec 한 뒤 place_actors() + save_dirty_packages."""
    gx, gy = GATE_N
    px, py = PLAZA
    hx, hy = HIDEOUT
    rx, ry = RUINS
    lx, ly = LIBRARY
    bx, by = BOMB
    ox, oy = OUTPOST
    kx, ky = CITADEL[0], CITADEL[1] + 1200  # 아성
    ps = find_actor(cls="PlayerStart")
    if ps:
        print("[scene] PlayerStart →", move_actor(ps, rx, ry + 600, 100, yaw=-90))  # 폐허 안, 남향 → 성문
    bed = find_actor(cls="BP_Bed_C")
    if bed:
        move_actor(bed, hx + 180, hy + 200, 0, yaw=90)
    move_npc("Guard", gx + 250, gy - 700, face=(gx, gy + 900))
    move_npc("James", px + 400, py + 350, face=PLAZA)
    move_npc("Moca", lx + 60, ly + 190, face=(lx + 60, ly - 120))
    move_npc("Skadi", bx - 700, by - 250, face=OUTPOST)
    move_npc("Elara", ox + 900, oy + 300, face=(ox, oy))  # 포로 우리
    move_npc("Commander_Vorg", ox - 300, oy - 300, face=(ox - OUT_HW, oy))
    move_npc("DemonLord", kx, ky + 700, face=(kx, ky - 3000))  # 왕좌


def spawner(zone, bp_name, x, y, max_alive, interval, radius, total=0, kills_for_flag=0, flag="", min_player=1500.0):
    """AEnemySpawner(C++) 1기. flag 는 킬 수 달성 시 story flag — 사이드퀘스트 complete_when {type: flag} 와 이름 일치."""
    cls = unreal.EditorAssetLibrary.load_blueprint_class(f"{ENEMY_BP}/{bp_name}")
    if cls is None:
        raise RuntimeError(f"enemy BP missing: {ENEMY_BP}/{bp_name} — tools/make_enemy_bps.py 먼저")
    a = EAS.spawn_actor_from_class(
        unreal.EnemySpawner, unreal.Vector(x, y, ground_z(x, y) + 10), unreal.Rotator(0, 0, 0)
    )
    a.set_editor_property("EnemyClass", cls)
    a.set_editor_property("MaxAlive", max_alive)
    a.set_editor_property("TotalSpawnLimit", total)
    a.set_editor_property("SpawnInterval", float(interval))
    a.set_editor_property("SpawnRadius", float(radius))
    a.set_editor_property("MinPlayerDistance", float(min_player))
    a.set_editor_property("KillsForFlag", kills_for_flag)
    a.set_editor_property("KillFlag", flag)
    return _finish(a, f"SCN_{zone}_spawner_{bp_name}")


def build_enemies():
    """서브퀘스트 토벌 대상 — 적은 PIE 에서 스포너가 주기 생성(에디터엔 스포너만)."""
    z = "enemy"
    # 도적 캠프(모닥불·상자·통) — 3명 유지, 3킬 → flag
    bx, by = BANDIT_CAMP
    campfire(z, bx, by, 1.2)
    hism(
        z,
        "cube",
        [
            (bx + dx, by + dy, 0, yaw, 0.7, 0.7, 0.7)
            for dx, dy, yaw in [(260, 120, 20), (-300, 200, 70), (180, -280, 0)]
        ],
        mat="wood",
    )
    hism(z, "cyl", [(bx - 220, by - 240, 0, 0, 0.5, 0.5, 0.8), (bx + 340, by - 60, 0, 0, 0.5, 0.5, 0.8)], mat="walnut")
    spawner(
        z, "BP_Bandit", bx, by, max_alive=3, interval=30, radius=700, kills_for_flag=3, flag="forest_raiders_cleared"
    )
    # 다리의 도살자 — 네임드 1기, 리스폰 없음(boss_killed 는 EnemyCharacter 가 npc_died 로 송신)
    spawner(z, "BP_OrcVagron", ORC_LAIR[0], ORC_LAIR[1], max_alive=1, interval=60, radius=300, total=1, min_player=0)
    # 죽은 숲 망령 — 3기 유지, 5킬 → flag
    spawner(
        z,
        "BP_KnightWraith",
        DEAD_FOREST[0],
        DEAD_FOREST[1],
        max_alive=3,
        interval=40,
        radius=1500,
        kills_for_flag=5,
        flag="wraiths_purified",
    )
    trigger("dead_forest", DEAD_FOREST[0], DEAD_FOREST[1], ext=(2500, 2500, 400))


def villager(kind, n, x, y, radius, face=None):
    """AVillagerCharacter 1명(BP_Villager_<kind>). VillagerID=<kind>_<n>. NavMesh 위로 투영해 허공·매몰·벽 속을 막는다."""
    cls = unreal.EditorAssetLibrary.load_blueprint_class(f"{VILLAGER_BP}/BP_Villager_{kind}")
    if cls is None:
        raise RuntimeError(f"villager BP missing: {VILLAGER_BP}/BP_Villager_{kind} — tools/make_villager_bps.py 먼저")
    want = unreal.Vector(x, y, ground_z(x, y) + 50)
    loc = unreal.NavigationSystemV1.project_point_to_navigation(WORLD, want, None, None, unreal.Vector(300, 300, 500))
    if abs(loc.z - want.z) < 0.01 and abs(loc.x - want.x) < 0.01 and abs(loc.y - want.y) < 0.01:
        print(f"[scene] villager {kind}_{n}: NavMesh 투영 실패 @({x:.0f},{y:.0f}) — 그대로 배치")
    yaw = unreal.MathLibrary.find_look_at_rotation(loc, unreal.Vector(face[0], face[1], loc.z)).yaw if face else 0.0
    a = EAS.spawn_actor_from_class(cls, unreal.Vector(loc.x, loc.y, loc.z + 92), unreal.Rotator(yaw=yaw))
    a.set_editor_property("VillagerID", f"{kind}_{n}")
    a.set_editor_property("WanderRadius", float(radius))
    return _finish(a, f"SCN_villager_{kind}_{n}")


def build_villager():
    """앰비언트 주민 12명 — 서버·LLM 0(AVillagerCharacter FSM). 정적 배치(스포너 없음). 상인은 Phase 3."""
    px, py = PLAZA
    gx, gy = GATE_N
    hx, hy = HIDEOUT
    lx, ly = LIBRARY
    # 광장 4 (기둥 링 700·의자 850 바깥) + 시장 가판대 2 (상자·통 무더기 피해서)
    for i, (x, y, r) in enumerate(
        [(px - 1100, py + 200, 500), (px - 200, py + 1100, 500), (px + 1000, py - 300, 450), (px - 400, py - 1000, 500),
         (px + 1250, py - 1550, 350), (px + 2400, py - 1100, 350)],
        1,
    ):
        villager("Townsfolk", i, x, y, r, face=PLAZA)
    # 성문 안 2 — 경비병(gx+250, gy-700) 옆, 계단·횃불 피해서. 웅크린 느낌으로 반경 작게
    villager("Refugee", 1, gx - 700, gy - 350, 250, face=(gx, gy))
    villager("Refugee", 2, gx + 750, gy - 450, 250, face=(gx, gy))
    # 은신처 3 — 문(북) 앞 2 + 안 1(제자리)
    villager("Refugee", 3, hx - 250, hy + 650, 300, face=(hx, hy))
    villager("Refugee", 4, hx + 300, hy + 750, 300, face=(hx, hy))
    villager("Refugee", 5, hx - 250, hy + 150, 0, face=(hx, hy - 150))
    # 도서관 1 — Moca(lx+60, ly+190)·탁자 서쪽, 서가 회랑 쪽
    villager("Scholar", 1, lx - 600, ly + 200, 400, face=(lx + 60, ly - 120))


def build_env():
    nav = find_actor(cls="NavMeshBoundsVolume")
    if nav:
        nav.modify(True)
        nav.set_actor_location(unreal.Vector(0, 0, 400), False, False)
        nav.set_actor_scale3d(unreal.Vector(255, 255, 18))  # 51000×51000×3600
        print("[scene] NavMeshBounds → 전 맵")
    fog = find_actor(cls="ExponentialHeightFog")
    if fog:
        comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if comp:
            fog.modify(True)
            comp.set_fog_density(0.02)
            comp.set_fog_inscattering_color(unreal.LinearColor(0.55, 0.5, 0.6, 1.0))
            print("[scene] fog 조정")


def build():
    ensure_hism_bp()
    if ONLY:
        for zone in ONLY:
            globals()[f"build_{zone}"]()
        return
    build_village()
    build_ruins()
    build_library()
    build_forest()
    build_river_bridge()
    build_outpost()
    build_citadel()
    build_roads()
    build_wilderness()
    build_enemies()
    build_villager()
    place_actors()
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

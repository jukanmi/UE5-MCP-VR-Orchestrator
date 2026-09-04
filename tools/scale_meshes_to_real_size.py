"""Art/Meshes/Items_Textured/*.glb 를 현실 치수로 리스케일한다.

Hunyuan3D 는 모든 메시를 약 2유닛 정육면체에 정규화해 내보낸다. UE 는 1유닛을 1cm 로 읽으므로
그대로 임포트하면 돌멩이도 창도 전부 2m 가 된다. 생성 단계에서 상대 크기가 소실되므로
아이템별 최대 치수를 표로 주고 원본 GLB 에 스케일을 굽는다.

UE 쪽 build_scale3d 로도 되지만 리빌드 트리거가 Python 에 노출돼 있지 않다. 원본을 고치면
어떤 임포터로 넣어도 맞으므로 이쪽이 낫다.

치수 단위는 cm, 메시의 최대 변 길이 기준이다.
"""

import os
import sys

import trimesh

MESH_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../Art/Meshes/Items_Textured"))

# 아이템별 현실 최대 치수(cm)
TARGET_SIZE_CM = {
    # 소모품
    "Bread": 22,
    "Bandage": 8,
    "WaterSkin": 25,
    "HealthPotion": 15,
    "ManaPotion": 15,
    "HerbTea": 12,
    "HerbExtract": 14,
    "Antidote": 12,
    "HolyWater": 18,
    "SmokeVial": 12,
    "SmokeBomb": 10,
    "RumBottle": 30,
    "Rations": 20,
    "SpellScroll": 28,
    # 무기
    "KnightSword": 100,
    "Cutlass_Pirate": 80,
    "GuardSpear": 200,
    "NavDagger": 30,
    "PoisonDagger": 30,
    "SkinningKnife": 22,
    "HuntingBow": 150,
    "Pistol": 35,
    "ForgeHammer": 45,
    "RepairHammer": 35,
    "Arrow_Bundle": 75,
    # 방어구·착용
    "Shield_Knight": 70,
    "GuardShield": 80,
    "DivingHelmet": 40,
    "FeatherCap": 30,
    "Glasses": 14,
    "StarPendant": 6,
    "Torch": 50,
    "Lantern": 30,
    "Lute": 70,
    # 문서·퀘스트
    "OathScroll": 30,
    "AncientScroll": 30,
    "ProphecyScroll": 30,
    "StarMap": 40,
    "PassDoc": 25,
    "HolyBook": 25,
    "TreasureKey": 12,
    "HonorFlower": 30,
    "GoldChest": 50,
    # 도구·잡화
    "CoinPouch": 12,
    "GoldCoins": 12,
    "BribeCoin": 4,
    "Compass": 8,
    "BrokenCompass": 8,
    "Telescope": 40,
    "MagnifyingGlass": 20,
    "Whistle": 6,
    "QuillPen": 25,
    "Lockpick": 12,
    "TrapDisarmKit": 25,
    "MagicGem": 8,
    "CrystalBall": 15,
    "AlchemistryVial": 18,
    "Microphone": 20,
    "WineCup": 18,
    "Whetstone": 18,
    "SoftBlanket": 40,
    "Rope": 30,
    "HerbBasket": 35,
    "WaterBucket": 35,
    "TarBucket": 35,
    "ShipWheel": 100,
    # 재료
    "Rock": 15,
    "IronOre": 20,
    "IronIngot": 25,
    "Plank": 100,
    "Leather": 40,
    "ShipBoard": 120,
}


def scale_one(path: str, target_cm: float):
    scene = trimesh.load(path)
    extents = scene.extents if hasattr(scene, "extents") else scene.bounding_box.extents
    current_max = float(max(extents))
    if current_max <= 0:
        return None

    # glTF 단위는 미터다. cm 를 그대로 구우면 UE 가 m->cm 변환하며 100배로 들어온다.
    factor = (target_cm / 100.0) / current_max
    scene.apply_scale(factor)
    scene.export(path)

    new_max = float(max(scene.extents))
    return current_max, new_max, factor


# 바닥에 놓였을 때 눈에 들어오는 하한. 동전·호루라기처럼 현실 치수가 4~8cm 인 것들은
# 실제 크기로 두면 플레이어가 찾지 못한다. 표에는 현실 치수를 남기고 적용할 때만 끌어올린다.
MIN_VISIBLE_CM = 15


def main():
    names = sorted(os.path.splitext(f)[0] for f in os.listdir(MESH_DIR) if f.endswith(".glb"))
    missing = [n for n in names if n not in TARGET_SIZE_CM]
    if missing:
        print(f"[중단] 치수 미정의 {len(missing)}종: {', '.join(missing)}")
        return 1

    print(f"=== {len(names)}종 현실 치수 리스케일 ===")
    for n in names:
        target = max(TARGET_SIZE_CM[n], MIN_VISIBLE_CM)
        result = scale_one(os.path.join(MESH_DIR, f"{n}.glb"), target)
        if result is None:
            print(f"  {n:18s} 건너뜀 (크기 0)")
            continue
        before, after, factor = result
        print(f"  {n:18s} {before:8.3f} -> {after * 100:6.1f}cm  (x{factor:.5f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

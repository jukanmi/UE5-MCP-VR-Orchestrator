"""아이템 레지스트리(ItemRegistry.csv) 기반 ComfyUI 2D 아이콘 클린 아이솔레이션 생성기 (v3).

특징:
- 피규어/스탠드/받침대/캐릭터/사람 강력 차단 네거티브 가중치 (1.6)
- 순수 단일 2D 게임 인벤토리 스프라이트 아이콘 포지티브 튜닝
- 72종 전수 명사 중심 클린 프롬프트
- 모델 자동/수동 선택 지원 (waiIllustriousSDXL 또는 counterfeitxl)
"""

import asyncio
import csv
import json
import os
import random
import sys
import time
from urllib.parse import urlencode

import httpx

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

COMFYUI_URL = os.environ.get("COMFYUI_URL", "http://127.0.0.1:8188").rstrip("/")
CSV_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../Content/Data/Items/ItemRegistry.csv"))
# 원본 PNG 는 Content/ 밖에 둔다 — Content 안에 있으면 UE 가 자동 임포트를 걸어
# 같은 그림이 png 와 uasset 두 벌로 저장소에 쌓인다. 임포트 결과만 /Game/UI/Icons/Items 로 들어간다.
OUTPUT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../Art/Icons/Items"))

# 72종 전수 순수 물품 명사 묘사 (캐릭터/소품 유발 단어 완전 배제)
ITEM_PROMPT_HINTS = {
    # 코어 베이스라인 5종
    "Bread": "a single freshly baked golden brown crusty bread loaf, bakery food, floating in dark void, no cutting board, no table, no wood base",
    "Bandage": "a single roll of clean white linen medical gauze bandage cloth, first aid supply, floating in dark void, no bottle, no capsule, no potion",
    "WaterSkin": "a single curved medieval brown leather waterskin canteen bota bag with wooden plug and leather strap, floating in dark void, no metal flask",
    "CoinPouch": "a single tied medieval brown leather coin pouch drawstring purse filled with gold coins, fantasy RPG item, floating in dark void",
    "Torch": "a single wooden handheld torch stick with burning glowing flame",
    # 코어 5인 전용
    "KnightSword": "a single medieval broadsword weapon with sharp double-edged steel blade, ornate silver crossguard, black leather grip, fantasy RPG weapon, floating in dark void",
    "Shield_Knight": "a single medieval knight heater shield with polished steel plate and painted golden lion crest, defensive armor plate, floating in dark void",
    "HealthPotion": "a single round glass potion flask bottle filled with glowing red liquid healing potion with cork stopper, floating in dark void",
    "OathScroll": "a single rolled antique parchment paper scroll tied with red wax seal ribbon, floating in dark void",
    "Cutlass_Pirate": "a single curved pirate cutlass sabre sword with steel blade and golden d-guard knuckle handle, pirate weapon, floating diagonally in dark void, no basket, no bowl",
    "Pistol": "a single antique wooden flintlock pistol handgun with engraved brass barrel, floating in dark void",
    "RumBottle": "a single vintage dark glass rum liquor bottle with cork stopper, floating in dark void",
    "TreasureKey": "a single ornate antique brass skeleton key with decorative circular bow and notched bit, pirate treasure key, floating diagonally in dark void, single key, no extra keys",
    "HerbTea": "a single ceramic teacup filled with hot herbal green tea, floating in dark void",
    "SmokeBomb": "a single spherical black ceramic smoke bomb grenade with lit burning fuse, emitting dark gray smoke clouds, ninja tool, floating in dark void, no person, no character, no girl",
    "HerbBasket": "a single small woven wicker basket filled with fresh green medicinal herbs, floating in dark void",
    "Compass": "a single antique brass pocket compass with glass face and needle, floating in dark void",
    "StarMap": "a single circular bronze astrological star map plate with engraved celestial lines, floating in dark void",
    "NavDagger": "a single sleek sailor navigation dagger knife weapon with steel blade and brass hilt, floating in dark void",
    "Telescope": "a single cylindrical antique brass collapsible spyglass telescope, side profile view showing full extended brass tube, optical tool, floating diagonally in dark void",
    "GuardSpear": "a single long medieval polearm guard spear weapon with polished steel spearhead tip and long straight wooden shaft, pole weapon, floating diagonally in dark void, no sword",
    "GuardShield": "a single large medieval wooden kite shield armor with iron banding and heavy rivets, front view, defensive gear, floating vertically in dark void, no lantern, no crystal",
    "Whistle": "a single shiny silver metallic pea whistle with mouthpiece and hanging ring, security guard whistle tool, floating in dark void, no medallion, no chain",
    "PassDoc": "a single official wax-sealed paper travel pass permit passport certificate, floating in dark void",
    "Leather": "a single flat square sheet of brown tanned leather hide pelt, rough textured animal hide, floating in dark void",
    # 크래프팅/파밍/퀘스트 기초
    "Rock": "a single rough natural gray granite mineral stone rock pebble, raw stone, floating in dark void, no crystal, no magic, no blue glow",
    "IronIngot": "a single rectangular solid iron metal ingot brick bar with smooth gray metallic surface and bevel edges, blacksmith crafting ingot, floating diagonally in dark void, no pillar, no needle",
    "WaterBucket": "a single rustic wooden bucket with metal handle filled with water",
    "HonorFlower": "a single glowing blue wild flower blossom with stem and petals",
    "DivingHelmet": "a single antique heavy brass copper deep sea diving helmet with round front glass porthole grill and copper bolts, steampunk gear, floating in dark void, no base, no stand, no pedestal, no frame",
    "GoldChest": "a single closed wooden treasure chest bound with gold trim and padlock",
    "Microphone": "a single magical handheld blue crystal microphone wand staff, cylindrical silver metal handle with glowing round blue crystal sphere top, fantasy audio wand, floating vertically in dark void",
    "SoftBlanket": "a single neatly folded thick fluffy soft wool blanket, plaid pattern, cozy bedding item, floating in dark void, no orb, no ball, no crystal",
    "ShipWheel": "a single wooden maritime ship steering wheel helm",
    "BrokenCompass": "a single antique tarnished brass pocket compass with shattered cracked glass dial face and bent broken needle, weathered damaged compass, floating in dark void",
    "BribeCoin": "a single shiny stamped silver coin token",
    # 보조 10인 전용
    "MagnifyingGlass": "a single brass handheld magnifying glass with clear lens",
    "GoldCoins": "a single heavy velvet coin pouch overflowing with gleaming gold coins",
    "MagicGem": "a single faceted cut glowing blue magic crystal gem",
    "AncientScroll": "a single weathered ancient parchment scroll with glowing runes",
    "QuillPen": "a single elegant feather quill pen with sharp metal writing nib",
    "Glasses": "a single pair of round wireframe gold reading spectacles glasses with clear glass lenses, scholar accessory item, floating in dark void, no person, no character, no girl, no face, no head, no book",
    "PoisonDagger": "a single curved sharp assassin dagger knife blade coated with dripping glowing green toxic venom poison, rogue weapon, floating diagonally in dark void, no bottle, no potion",
    "Lockpick": "a single pair of slender steel thief lockpick tools with tension wrench and pick needle, rogue burglar tool, floating in dark void, no fork, no knife, no cutlery",
    "SmokeVial": "a single small glass vial bottle containing swirling dark purple smoke mist with cork stopper, potion item, floating in dark void, no stand, no pedestal, no base",
    "HolyWater": "a single ornate crystal holy water flask with golden cross cap",
    "HolyBook": "a single thick leather-bound scripture book with golden holy cross",
    "ForgeHammer": "a single heavy blacksmith steel sledge hammer with square metal hammerhead and sturdy wooden handle, crafting tool, floating diagonally in dark void, no anvil, no machine, no base",
    "Whetstone": "a single rectangular fine-grain sharpening whetstone waterstone block for sharpening knives, blade honing tool, floating diagonally in dark void, no spear, no shovel",
    "AlchemistryVial": "a single clear glass alchemy test tube vial filled with amber liquid",
    "HerbExtract": "a single dropper bottle of concentrated green herbal medicine liquid",
    "Antidote": "a single glass medicine bottle filled with glowing green cure antidote",
    "Lute": "a single wooden acoustic musical lute instrument with strings",
    "FeatherCap": "a single stylish medieval bard feathered beret cap hat adorned with a tall colorful ostrich feather, velvet fabric, headwear item, floating in dark void, no person, no character, no girl, no face",
    "WineCup": "a single ornate silver goblet wine chalice cup",
    "TrapDisarmKit": "a single open leather tool roll case containing medieval thief wire cutters, pliers, tension probes, trap disarming toolkit, floating in dark void, no laser, no tech",
    "HuntingBow": "a single wooden curved hunting recurve bow weapon with taut bowstring and leather grip, archery weapon, floating diagonally in dark void, no stand, no pedestal, no base",
    "TarBucket": "a single rusty iron bucket filled with thick viscous black tar pitch, shipbuilding repair material, floating in dark void, no water, no blue",
    "ShipBoard": "a single flat rectangular sturdy wooden ship plank board",
    "RepairHammer": "a single wooden carpenter claw repair hammer with steel hammerhead and wooden handle, woodworking tool, floating diagonally in dark void, single hammer, no second hammer, no ground",
    "CrystalBall": "a single mystical glowing translucent crystal sphere ball",
    "ProphecyScroll": "a single dark mystical parchment scroll tied with purple cord",
    "StarPendant": "a single silver necklace pendant shaped like an eight-pointed star",
    # 범용 소모품/원자재
    "ManaPotion": "a single round glass potion bottle filled with glowing blue magical mana liquid with cork",
    "Rations": "a single brown canvas survival ration pack with dried beef jerky and hardtack biscuit, fantasy travel food, floating in dark void, no plastic wrapper, no modern bag",
    "SpellScroll": "a single rolled antique magic parchment spell scroll glowing with arcane blue runic energy, tied with leather cord, floating in dark void, no book, no sword",
    "Rope": "a single coiled bundle roll of thick brown braided hemp rope cord, adventuring survival gear, floating in dark void, no fruit, no pumpkin, no red",
    "Lantern": "a single vintage metal oil lantern with glass casing and burning flame inside",
    "SkinningKnife": "a single small curved steel hunting skinning knife with sharp blade and bone handle, hunter field tool, floating diagonally in dark void, no person, no character, no human, no girl",
    "Arrow_Bundle": "a single leather quiver bundle filled with straight wooden hunting arrows with feather fletching and steel arrowheads, archery gear, floating diagonally in dark void, no snowflake, no star",
    "IronOre": "a single chunk of raw unrefined gray iron mineral rock ore with metallic flecks",
    "Plank": "a single smooth cut rectangular lumber timber wood plank",
    "Leather": "a single rolled sheet of tanned brown animal leather hide",
}


def build_icon_prompt(item_id: str, display_name: str, desc: str, item_type: str) -> str:
    hint = ITEM_PROMPT_HINTS.get(item_id)
    if not hint:
        hint = f"a single {item_id}, floating in dark void"

    positive_prompt = (
        f"{hint}, "
        "masterpiece, high quality digital fantasy art, single centered object, isolated on dark plain background, "
        "clean lighting, sharp details, 8k resolution"
    )
    return positive_prompt


def get_negative_prompt() -> str:
    return (
        "(pedestal, stand, base, plinth, platform, table, desk, ground, floor, surface, tray, plate, board:1.6), "
        "(figure, figurine, statue, miniature, model:1.6), "
        "(person, human, man, woman, girl, boy, character, body, hands, fingers:1.7), "
        "(barrel, keg, dice, puddle, spill, clutter, extra objects, multiple objects:1.6), "
        "(scene, environment, room, interior, landscape, horizon, outdoor, sky, wall:1.5), "
        "(frame, border, emblem, badge, crest, medal, sticker, UI box, dialog, window:1.6), "
        "(text, label, watermark, signature, font, letters, numbers:1.6), "
        "blurry, low quality, cropped, out of frame, realistic photo"
    )


async def get_best_checkpoint(client: httpx.AsyncClient) -> str:
    try:
        resp = await client.get(f"{COMFYUI_URL}/object_info/CheckpointLoaderSimple")
        if resp.status_code == 200:
            ckpts = (
                resp.json()
                .get("CheckpointLoaderSimple", {})
                .get("input", {})
                .get("required", {})
                .get("ckpt_name", [[]])[0]
            )
            preferred = [c for c in ckpts if "counterfeit" in c.lower() or "nova" in c.lower()]
            if preferred:
                return preferred[0]
            if ckpts:
                return ckpts[0]
    except Exception as e:
        print(f"[경고] 모델 목록 조회 실패: {e}")
    return "SDXL/counterfeitxl_v25.safetensors"


async def generate_single_item(
    client: httpx.AsyncClient, item: dict, checkpoint: str, index: int, total: int, force: bool = False
) -> bool:
    item_id = item["ItemID"]
    display_name = item["DisplayName"]
    out_file = os.path.join(OUTPUT_DIR, f"{item_id}.png")

    if not force and os.path.exists(out_file):
        print(f"[{index}/{total}] ⏩ {display_name}({item_id}) 이미 존재함 -> 건너뜀")
        return True

    prompt_text = build_icon_prompt(item_id, display_name, item.get("Description", ""), item.get("ItemType", ""))
    negative_text = get_negative_prompt()

    seed = random.randint(1, 1000000000)

    workflow = {
        "3": {
            "inputs": {
                "seed": seed,
                "steps": 25,
                "cfg": 7.5,
                "sampler_name": "euler_ancestral",
                "scheduler": "normal",
                "denoise": 1.0,
                "model": ["4", 0],
                "positive": ["6", 0],
                "negative": ["7", 0],
                "latent_image": ["5", 0],
            },
            "class_type": "KSampler",
        },
        "4": {"inputs": {"ckpt_name": checkpoint}, "class_type": "CheckpointLoaderSimple"},
        "5": {"inputs": {"width": 768, "height": 768, "batch_size": 1}, "class_type": "EmptyLatentImage"},
        "6": {"inputs": {"text": prompt_text, "clip": ["4", 1]}, "class_type": "CLIPTextEncode"},
        "7": {"inputs": {"text": negative_text, "clip": ["4", 1]}, "class_type": "CLIPTextEncode"},
        "8": {"inputs": {"samples": ["3", 0], "vae": ["4", 2]}, "class_type": "VAEDecode"},
        "9": {"inputs": {"filename_prefix": f"ItemIcon_{item_id}", "images": ["8", 0]}, "class_type": "SaveImage"},
    }

    print(f"[{index}/{total}] 🎨 생성 시작: {display_name} ({item_id})...")
    start_time = time.time()

    resp = await client.post(f"{COMFYUI_URL}/prompt", json={"prompt": workflow, "client_id": f"batch_{item_id}"})
    if resp.status_code != 200:
        print(f"[{index}/{total}] ❌ 큐 등록 실패: {resp.text}")
        return False

    prompt_id = resp.json().get("prompt_id")

    while time.time() - start_time < 90:
        await asyncio.sleep(0.8)
        hist_resp = await client.get(f"{COMFYUI_URL}/history/{prompt_id}")
        if hist_resp.status_code == 200:
            hist_data = hist_resp.json()
            if prompt_id in hist_data:
                task = hist_data[prompt_id]
                outputs = task.get("outputs", {})
                if "9" in outputs and "images" in outputs["9"]:
                    img_info = outputs["9"]["images"][0]
                    img_url = f"{COMFYUI_URL}/view?{urlencode(img_info)}"
                    img_data = await client.get(img_url)
                    if img_data.status_code == 200:
                        with open(out_file, "wb") as f:
                            f.write(img_data.content)
                        elapsed = time.time() - start_time
                        print(f"[{index}/{total}] ✅ 저장 완료: {out_file} ({elapsed:.1f}초)")
                        return True

    print(f"[{index}/{total}] ⚠️ 시간 초과: {item_id}")
    return False


async def main(target: str = "sample"):
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    if not os.path.exists(CSV_PATH):
        print(f"CSV 파일을 찾을 수 없습니다: {CSV_PATH}")
        return

    items = []
    with open(CSV_PATH, "r", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row.get("ItemID"):
                items.append(row)

    if target == "sample":
        target_ids = ["KnightSword", "HealthPotion", "Pistol", "Leather", "Shield_Knight"]
        items = [item for item in items if item["ItemID"] in target_ids]
        force = True
        print("=== [클린 아이솔레이션 v3 표본 검증 모드 (5종)] ===")
    elif target == "all" or target == "force":
        force = True
        print(f"=== [클린 아이솔레이션 v3 전량 72종 모드 (덮어쓰기={force})] ===")
    else:
        target_ids = [t.strip() for t in target.split(",")]
        items = [item for item in items if item["ItemID"] in target_ids]
        force = True
        print(f"=== [지정 아이템 재생성 모드 ({len(items)}종): {', '.join(target_ids)}] ===")

    total = len(items)
    print(f"출력 경로: {OUTPUT_DIR}\n")

    async with httpx.AsyncClient(timeout=120) as client:
        checkpoint = await get_best_checkpoint(client)
        print(f"사용 모델: {checkpoint}\n")

        success_count = 0
        for i, item in enumerate(items, 1):
            success = await generate_single_item(client, item, checkpoint, i, total, force=force)
            if success:
                success_count += 1

    print(f"\n=== 생성 작업 완료: {success_count}/{total} 성공 ===")


if __name__ == "__main__":
    mode_arg = sys.argv[1] if len(sys.argv) > 1 else "sample"
    asyncio.run(main(mode_arg))

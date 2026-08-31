"""아이템 레지스트리(ItemRegistry.csv) 기반 ComfyUI 아이콘 일괄 생성 스크립트."""

import asyncio
import csv
import json
import os
import re
import sys
import time
from urllib.parse import urlencode

import httpx

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

COMFYUI_URL = os.environ.get("COMFYUI_URL", "http://127.0.0.1:8188").rstrip("/")
CSV_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../Content/Data/Items/ItemRegistry.csv"))
OUTPUT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../Content/UI/Icons/Items"))

# 아이템별 영문 프롬프트 매핑 힌트 (더 선명하고 정확한 2D RPG 게임 아이콘 생성용)
ITEM_PROMPT_HINTS = {
    "Bread": "a loaf of rustic rye bread on a wooden table, fantasy rpg food icon, crisp crust",
    "Bandage": "rolled linen cloth bandage with a clean medical herb leaf, rpg healing item icon",
    "WaterSkin": "a leather waterskin canteen with a brass cap and strap, adventuring gear icon",
    "CoinPouch": "a small leather pouch overflowing with shiny silver coins, rpg money icon",
    "Torch": "a wooden torch with burning bright flame and resin, dark background, glowing light",
    "KnightSword": "a gleaming steel knight longsword with ornate silver hilt and crossguard, game icon",
    "Shield_Knight": "a heraldic medieval knight heater shield with royal crest emblem, polished steel",
    "HealthPotion": "a glass flask filled with glowing red liquid healing potion, cork stopper, magic sparkle",
    "OathScroll": "an antique parchment oath scroll tied with a red wax knight crest seal",
    "Cutlass_Pirate": "a curved pirate cutlass sword with a brass basket hilt, maritime weapon icon",
    "Pistol": "a vintage flintlock pistol with engraved wood stock and brass barrel, antique firearm",
    "RumBottle": "a dark glass bottle of aged sugarcane rum with a skull pirate label",
    "TreasureKey": "an ornate antique rusty iron skeleton key for a pirate treasure chest",
    "HerbTea": "a steaming ceramic mug of soothing herbal tea with mint and chamomile leaves",
    "SmokeBomb": "a small ceramic ninja smoke bomb grenade with a burning fuse, emitting dark mist",
    "HerbBasket": "a woven wicker basket filled with fresh green medicinal herbs and flowers",
    "Compass": "an antique brass pocket compass with ornate nautical navigation needle",
    "StarMap": "an astrological celestial star map chart plate with glowing constellation lines",
    "NavDagger": "a sleek sailor navigation dagger with brass crossguard and leather grip",
    "Telescope": "an antique brass collapsible spyglass telescope, nautical astronomy tool",
    "GuardSpear": "a medieval town guard spear halberd with polished steel spearhead and wooden shaft",
    "GuardShield": "a sturdy wooden guard tower kite shield with iron reinforced rim",
    "Whistle": "a shiny metal security whistle on a cord, guard emergency alarm tool",
    "PassDoc": "an official wax-sealed royal checkpoint travel permit passport document",
    "Rock": "a rugged gray mineral stone rock pebble, rough texture, single object",
    "IronIngot": "a single polished rectangular refined iron metal ingot bar, metallic sheen",
    "WaterBucket": "a rustic wooden bucket filled with clear splashing fresh water",
    "HonorFlower": "a delicate glowing blue mountain wild flower blossom of honor, fantasy herb",
    "DivingHelmet": "an antique copper brass diving helmet with round glass viewing portholes",
    "GoldChest": "an ornate wooden treasure chest overflowing with gold coins and jewels, iron bands",
    "Microphone": "a crystal studio microphone with a silver stand, glowing translucent quartz body",
    "SoftBlanket": "a folded soft cream wool blanket with knitted texture and fringed edge",
    "ShipWheel": "a sturdy oak ship steering wheel helm with brass hub and eight spokes",
    "BrokenCompass": "a cracked antique brass compass with a bent broken needle and shattered glass",
    "BribeCoin": "a few tarnished silver bribe coins stacked, dim shady lighting",
    "MagnifyingGlass": "an antique brass handled magnifying glass with a clear convex lens",
    "GoldCoins": "a heavy velvet pouch spilling stacks of shining gold coins",
    "MagicGem": "a faceted glowing violet magic gemstone radiating arcane light",
    "AncientScroll": "a weathered papyrus scroll covered in ancient runic script, frayed edges",
    "QuillPen": "a white feather quill pen resting beside a small inkpot",
    "Glasses": "a pair of round wire-rimmed scholar spectacles with thin brass frames",
    "PoisonDagger": "a slender assassin dagger with a blackened blade dripping green venom",
    "Lockpick": "a set of slim steel lockpick tools in a leather roll, thief gear",
    "SmokeVial": "a small glass throwing vial filled with swirling dense gray smoke",
    "HolyWater": "a blessed crystal vial of glowing holy water with a golden cross stopper",
    "HolyBook": "a thick leather-bound holy scripture with gold leaf edges and an embossed goddess emblem",
    "ForgeHammer": "a heavy blacksmith forging hammer with a scarred steel head and worn wooden handle",
    "Whetstone": "a rectangular gray sharpening whetstone block with worn honed surface",
    "AlchemistryVial": "an empty clear glass alchemy test tube in a small wooden rack",
    "HerbExtract": "a small vial of thick concentrated green herbal extract, cork stopper",
    "Antidote": "a teal glass antidote vial with a snake emblem label, cure potion",
    "Lute": "a wooden renaissance lute with a rounded body and taut strings, bard instrument",
    "FeatherCap": "a velvet bard cap with a long colorful plume feather",
    "WineCup": "an engraved silver wine goblet filled with dark red wine",
    "TrapDisarmKit": "a compact trap disarming toolkit with pliers, wire cutters and picks in a leather case",
    "HuntingBow": "a curved wooden hunting bow with a taut bowstring and leather grip",
    "TarBucket": "a wooden bucket of thick black pitch tar with a coating brush",
    "ShipBoard": "a stack of thick oak repair planks for a ship deck, cut lumber",
    "RepairHammer": "a carpenter claw hammer with a steel head and oak handle, beside iron nails",
    "CrystalBall": "a polished fortune telling crystal ball on an ornate silver stand, swirling mist inside",
    "ProphecyScroll": "an ominous prophecy scroll with faded arcane glyphs and a cracked black wax seal",
    "StarPendant": "a silver star-shaped pendant on a fine chain, glowing with starlight",
    "ManaPotion": "a glass flask filled with glowing blue mana liquid, cork stopper, arcane sparkle",
    "Rations": "dried jerky strips and hard travel biscuits wrapped in cloth, adventurer rations",
    "SpellScroll": "a rolled magic spell scroll bound with a glowing rune ribbon",
    "Rope": "a coiled thick hemp rope with frayed ends, climbing gear",
    "Lantern": "an antique brass oil lantern with a glass pane and warm burning flame",
    "SkinningKnife": "a small curved skinning knife with a bone handle, hunting tool",
    "Arrow_Bundle": "a bundle of feathered wooden arrows tied together, steel broadheads",
    "IronOre": "a chunk of raw iron ore rock with rust-colored metallic veins",
    "Plank": "a stack of cut wooden planks, planed lumber boards",
    "Leather": "a rolled piece of tanned brown leather hide with stitched edge",
}


def build_icon_prompt(item_id: str, item_type: str) -> str:
    """아이템 정보로부터 최적의 RPG 아이콘 프롬프트를 조립합니다."""
    hint = ITEM_PROMPT_HINTS.get(item_id)
    if not hint:
        # 폴백은 ItemID 를 띄어쓰기로 풀어 쓴 영문만 쓴다.
        # DisplayName·Description 은 한국어라 SDXL 의 CLIP 이 해석하지 못하고
        # 무의미한 토큰으로 들어가 엉뚱한 그림이 나온다.
        words = re.sub(r"[_\d]+", " ", item_id)
        words = re.sub(r"(?<=[a-z])(?=[A-Z])", " ", words).lower().strip()
        hint = f"a detailed {words}, {item_type.lower()} item"

    # 고품질 아이콘 스타일 태그 결합
    positive_prompt = (
        f"game item icon, {hint}, "
        "isolated on dark neutral studio background, 2d game art, high quality, sharp focus, "
        "detailed texture, professional digital painting, fantasy rpg inventory asset, clean lighting, 8k resolution"
    )
    return positive_prompt


async def get_best_checkpoint(client: httpx.AsyncClient) -> str:
    """ComfyUI에서 최적의 SDXL 모델을 찾습니다."""
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
            # 카툰/일러스트/SDXL 우선 선택
            preferred = [
                c
                for c in ckpts
                if "wai" in c.lower()
                or "counterfeit" in c.lower()
                or "nova" in c.lower()
                or "pony" in c.lower()
                or "xl" in c.lower()
            ]
            if preferred:
                return preferred[0]
            if ckpts:
                return ckpts[0]
    except Exception as e:
        print(f"[경고] 모델 목록 조회 실패: {e}")
    return "SDXL/counterfeitxl_v25.safetensors"


async def generate_single_item(client: httpx.AsyncClient, item: dict, checkpoint: str, index: int, total: int) -> bool:
    """단일 아이템의 아이콘 이미지를 생성하고 저장합니다."""
    item_id = item["ItemID"]
    display_name = item["DisplayName"]
    out_file = os.path.join(OUTPUT_DIR, f"{item_id}.png")

    if os.path.exists(out_file):
        print(f"[{index}/{total}] ⏩ {display_name}({item_id}) 이미 존재함 -> 건너뜀")
        return True

    prompt_text = build_icon_prompt(item_id, item["ItemType"])
    negative_text = (
        "blurry, low quality, deformed, disfigured, text, watermark, signature, cropped, bad anatomy, frame, border"
    )

    import random

    seed = random.randint(1, 1000000000)

    # 512x512 또는 768x768 아이콘 해상도 (인벤토리 슬롯에 최적화)
    workflow = {
        "3": {
            "inputs": {
                "seed": seed,
                "steps": 20,
                "cfg": 6.5,
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

    # 1. 큐 등록
    resp = await client.post(f"{COMFYUI_URL}/prompt", json={"prompt": workflow, "client_id": f"batch_{item_id}"})
    if resp.status_code != 200:
        print(f"[{index}/{total}] ❌ 큐 등록 실패: {resp.text}")
        return False

    prompt_id = resp.json().get("prompt_id")

    # 2. 완료 대기
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
                    # 이미지 다운로드
                    img_data = await client.get(img_url)
                    if img_data.status_code == 200:
                        with open(out_file, "wb") as f:
                            f.write(img_data.content)
                        elapsed = time.time() - start_time
                        print(f"[{index}/{total}] ✅ 저장 완료: {out_file} ({elapsed:.1f}초)")
                        return True

    print(f"[{index}/{total}] ⚠️ 시간 초과: {item_id}")
    return False


async def main(limit: int = 0):
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    if not os.path.exists(CSV_PATH):
        print(f"CSV 파일을 찾을 수 없습니다: {CSV_PATH}")
        return

    items = []
    # utf-8-sig — CSV 에 BOM 이 있어 utf-8 로 읽으면 첫 헤더가 "﻿Name" 이 된다.
    with open(CSV_PATH, "r", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row.get("ItemID"):
                items.append(row)

    if limit > 0:
        items = items[:limit]

    total = len(items)
    print(f"=== 아이템 아이콘 일괄 생성 시작 (총 {total}종) ===")
    print(f"출력 경로: {OUTPUT_DIR}\n")

    async with httpx.AsyncClient(timeout=120) as client:
        # 가용 체크포인트 선택
        checkpoint = await get_best_checkpoint(client)
        print(f"사용 모델: {checkpoint}\n")

        success_count = 0
        for i, item in enumerate(items, 1):
            success = await generate_single_item(client, item, checkpoint, i, total)
            if success:
                success_count += 1

    print(f"\n=== 생성 작업 완료: {success_count}/{total} 성공 ===")


if __name__ == "__main__":
    limit_arg = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    asyncio.run(main(limit_arg))

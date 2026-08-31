import asyncio
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

COMFYUI_URL = "http://127.0.0.1:8188"
OUTPUT_DIR = r"c:\github\UE5_MCP_VR\Art\Icons\Items"

ITEMS = {
    "Glasses": "glasses, eyeglasses, round eyewear, gold frame, clear lenses, resting on surface",
    "FeatherCap": "feathered hat, bard hat, beret, ostrich feather, colorful feather, velvet hat, headwear",
    "SkinningKnife": "hunting knife, skinning knife, dagger, bone handle, sharp curved steel blade",
    "SmokeBomb": "smoke bomb, ninja bomb, ceramic sphere, burning fuse, dark smoke, throwable weapon",
    "Cutlass_Pirate": "cutlass, pirate sword, curved sabre, brass handguard, steel blade, weapon",
    "Rock": "rock, stone pebble, gray mineral granite, rough surface, nature stone",
    "IronIngot": "iron ingot, metal bar, steel brick, metallic sheen, blacksmith material",
    "DivingHelmet": "diving helmet, deep sea helmet, copper brass helmet, porthole, steampunk",
    "Microphone": "microphone wand, crystal staff, glowing blue gem on stick, magical wand",
    "SoftBlanket": "folded blanket, wool quilt, soft cozy bedding, folded sheet",
    "BrokenCompass": "broken compass, cracked glass, loose needle, antique tarnished brass",
    "TrapDisarmKit": "tool kit, lockpick set, wire cutters, pliers, thief tools in leather roll",
    "HuntingBow": "hunting bow, wooden recurve bow, bowstring, archery weapon",
    "TarBucket": "bucket of black tar, iron pail, pitch, dark viscous liquid",
    "RepairHammer": "carpenter hammer, claw hammer, steel head, wooden handle, tool",
    "Rations": "travel rations, beef jerky, hardtack biscuit, trail food, canvas pouch",
    "SpellScroll": "magic scroll, rolled parchment, glowing blue runes, magical seal",
    "Rope": "coiled rope, hemp rope, braided cord, bundle of rope, survival gear",
    "GuardSpear": "spear, polearm, long wooden spear, steel spearhead, halberd",
    "GuardShield": "kite shield, wooden shield, iron rim, medieval defensive armor",
    "Whistle": "whistle, metal pea whistle, security whistle, silver whistle",
    "PoisonDagger": "poison dagger, dripping green venom, curved dagger blade, rogue weapon",
    "Lockpick": "lockpick tools, tension wrench, pick needle, thief tools",
    "SmokeVial": "glass vial bottle, purple smoke mist, magic potion flask",
    "ForgeHammer": "blacksmith hammer, sledgehammer, heavy metal hammerhead, forge tool",
    "Whetstone": "whetstone, sharpening stone, waterstone block, blade sharpening tool",
    "Bread": "loaf of bread, crusty brown bread, freshly baked bread loaf",
    "Bandage": "roll of bandage, medical gauze, white linen roll, first aid cloth",
    "WaterSkin": "leather waterskin, bota bag canteen, wooden plug, leather strap",
}

async def generate_item(client, item_id, tags):
    seed = random.randint(1, 1000000000)
    positive = f"no_humans, still_life, solo, game_item, item_icon, {tags}, dark_background, clean_lighting, masterpiece, high_quality"
    negative = "human, person, girl, boy, 1girl, 1boy, character, body, hands, face, legs, feet, text, watermark, signature, blurry, low_quality, bad_anatomy"
    
    wf = {
        "3": {
            "inputs": {
                "seed": seed, "steps": 24, "cfg": 7.0, "sampler_name": "euler_ancestral", "scheduler": "normal", "denoise": 1.0,
                "model": ["4", 0], "positive": ["6", 0], "negative": ["7", 0], "latent_image": ["5", 0]
            },
            "class_type": "KSampler"
        },
        "4": {"inputs": {"ckpt_name": "SDXL/waiIllustriousSDXL_v160.safetensors"}, "class_type": "CheckpointLoaderSimple"},
        "5": {"inputs": {"width": 768, "height": 768, "batch_size": 1}, "class_type": "EmptyLatentImage"},
        "6": {"inputs": {"text": positive, "clip": ["4", 1]}, "class_type": "CLIPTextEncode"},
        "7": {"inputs": {"text": negative, "clip": ["4", 1]}, "class_type": "CLIPTextEncode"},
        "8": {"inputs": {"samples": ["3", 0], "vae": ["4", 2]}, "class_type": "VAEDecode"},
        "9": {"inputs": {"filename_prefix": f"Danbooru_{item_id}", "images": ["8", 0]}, "class_type": "SaveImage"}
    }
    
    r = await client.post(f"{COMFYUI_URL}/prompt", json={"prompt": wf})
    pid = r.json().get("prompt_id")
    start = time.time()
    
    while time.time() - start < 60:
        await asyncio.sleep(0.8)
        hr = await client.get(f"{COMFYUI_URL}/history/{pid}")
        if pid in hr.json():
            outputs = hr.json()[pid].get("outputs", {})
            if "9" in outputs:
                img_info = outputs["9"]["images"][0]
                img_url = f"{COMFYUI_URL}/view?{urlencode(img_info)}"
                img_resp = await client.get(img_url)
                out_path = os.path.join(OUTPUT_DIR, f"{item_id}.png")
                with open(out_path, "wb") as f:
                    f.write(img_resp.content)
                print(f"✅ [{item_id}] 완료 ({time.time()-start:.1f}초)")
                return True
    return False

async def main():
    print(f"=== Danbooru no_humans 기반 정밀 보정 ({len(ITEMS)}종) ===")
    async with httpx.AsyncClient(timeout=60) as client:
        for item_id, tags in ITEMS.items():
            await generate_item(client, item_id, tags)

if __name__ == "__main__":
    asyncio.run(main())

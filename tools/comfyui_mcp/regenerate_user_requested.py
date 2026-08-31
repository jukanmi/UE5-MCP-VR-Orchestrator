"""사용자 요청 6종(KnightSword, Arrow_Bundle, RepairHammer, Rations, Whistle, Telescope) 정밀 재생성 스크립트."""

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

TASKS = [
    {
        "id": "KnightSword",
        "pos": "masterpiece, best quality, a single medieval longsword broadsword weapon, long double-edged shining polished steel blade, ornate silver crossguard with blue gem, black leather wrapped grip handle, fantasy RPG weapon, floating diagonally in dark void, solo object, dark plain background",
        "neg": "human, person, girl, boy, 1girl, 1boy, character, body, hands, holding, wielding, clothes, armor, warrior, floor, ground, stand, text, watermark"
    },
    {
        "id": "Arrow_Bundle",
        "pos": "masterpiece, best quality, a single brown leather quiver bag pouch filled with straight wooden hunting arrows, green feather fletchings, sharp steel arrowheads, archery ammo, fantasy RPG equipment, floating diagonally in dark void, solo object, dark plain background",
        "neg": "human, person, girl, boy, 1girl, 1boy, archer, character, body, hands, bow, circle, snowflake, wheel, weapon holder, floor, ground, text"
    },
    {
        "id": "RepairHammer",
        "pos": "masterpiece, best quality, a single wooden carpenter claw hammer tool with steel metal hammerhead and polished wooden handle, woodworking repair tool, fantasy RPG tool, floating diagonally in dark void, solo object, dark plain background",
        "neg": "box, container, shelf, machine, anvil, table, floor, ground, human, person, second hammer, stand, text, watermark"
    },
    {
        "id": "Rations",
        "pos": "masterpiece, best quality, a single wrapped medieval adventurer travel ration pack, dried beef jerky slices, hard bread biscuits, wrapped in rustic brown cloth and tied with string, fantasy food, floating in dark void, solo object, dark plain background",
        "neg": "plastic, modern packaging, bento box, camera, UI, text, barcode, label, human, person, table, plate, white box, text, watermark"
    },
    {
        "id": "Whistle",
        "pos": "masterpiece, best quality, a single antique polished brass pea whistle with metal loop ring and mouthpiece, security guard whistle tool, shining golden metallic surface, fantasy RPG item, floating diagonally in dark void, solo object, dark plain background",
        "neg": "box, plastic, machine, blue case, electronic, lock, logo, text, necklace, chain, human, person, wall, text, watermark"
    },
    {
        "id": "Telescope",
        "pos": "masterpiece, best quality, a single antique brass handheld collapsible spyglass telescope, side profile view of full cylindrical brass tube with glass lenses, sailor navigation tool, floating diagonally in dark void, solo object, dark plain background",
        "neg": "tripod, stand, mount, pedestal, pillar, human, person, girl, boy, 1girl, 1boy, holding, looking through, eyes, character, table, floor, text"
    }
]

async def generate(client, item):
    seed = random.randint(1, 1000000000)
    wf = {
        "3": {
            "inputs": {
                "seed": seed, "steps": 26, "cfg": 7.0, "sampler_name": "euler_ancestral", "scheduler": "normal", "denoise": 1.0,
                "model": ["4", 0], "positive": ["6", 0], "negative": ["7", 0], "latent_image": ["5", 0]
            },
            "class_type": "KSampler"
        },
        "4": {"inputs": {"ckpt_name": "SDXL/counterfeitxl_v25.safetensors"}, "class_type": "CheckpointLoaderSimple"},
        "5": {"inputs": {"width": 768, "height": 768, "batch_size": 1}, "class_type": "EmptyLatentImage"},
        "6": {"inputs": {"text": item["pos"], "clip": ["4", 1]}, "class_type": "CLIPTextEncode"},
        "7": {"inputs": {"text": item["neg"], "clip": ["4", 1]}, "class_type": "CLIPTextEncode"},
        "8": {"inputs": {"samples": ["3", 0], "vae": ["4", 2]}, "class_type": "VAEDecode"},
        "9": {"inputs": {"filename_prefix": f"Regen_{item['id']}", "images": ["8", 0]}, "class_type": "SaveImage"}
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
                out_path = os.path.join(OUTPUT_DIR, f"{item['id']}.png")
                with open(out_path, "wb") as f:
                    f.write(img_resp.content)
                print(f"✅ [{item['id']}] 완료 ({time.time()-start:.1f}초)")
                return True
    return False

async def main():
    print(f"=== 사용자 요청 6종 아이콘 정밀 재생성 시작 ===")
    async with httpx.AsyncClient(timeout=60) as client:
        for item in TASKS:
            await generate(client, item)
    print("=== 모든 요청 아이템 재생성 완료 ===")

if __name__ == "__main__":
    asyncio.run(main())


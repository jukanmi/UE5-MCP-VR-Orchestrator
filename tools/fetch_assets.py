"""무료(CC0) 외부 에셋 다운로드 — RawAssets/ 에 원본 보관(미추적). 멱등: 이미 받은 폴더/zip 은 건너뜀.

  python tools/fetch_assets.py            # 전부
  python tools/fetch_assets.py monsters   # 이름 부분 일치만

소스: Quaternius(Google Drive 공유 폴더, gdown) · Kenney(직접 zip). 전부 CC0 — 출처 표기 의무 없음.
Drive 는 익명 쿼터가 빡빡하다 → gdown 은 목록만 쓰고 본체는 usercontent 경로로 받는다(fetch_gdrive 주석).
itch.io 배포분(Fantasy Props Megakit, Universal Animation Library, Bestiary Dungeon Monsters)은 세션 서명 URL 이라 여기선 안 받는다 — 브라우저에서 수동.
UE 임포트 전 `python tools/mesh_doctor.py diagnose RawAssets/<pack>/FBX` 로 점검.
"""

import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "RawAssets"

# (폴더명, 종류, URL, 용도)
ASSETS = [
    (
        "quaternius_easy_enemy",
        "gdrive",
        "https://drive.google.com/drive/folders/1VbJIslXPWK-1KybQN6yezZrfJcw608qe",
        "적: 애니 포함 적 5종 (오크·해골 등)",
    ),
    (
        "quaternius_rpg_characters",
        "gdrive",
        "https://drive.google.com/drive/folders/1MIRQXLfTd21HMI5rwOb6Xy0rv0xv1m8b",
        "적: 도적·기사 인간형 6종 (리깅+애니)",
    ),
    (
        "quaternius_ultimate_modular_characters",
        "gdrive",
        "https://drive.google.com/drive/folders/1USAAquX2JJWuA2m6zol0KUkFe3UkZ8zX",
        "캐릭터 11종 + 애니 24종 (모듈 조합)",
    ),
    (
        "quaternius_animated_zombie",
        "gdrive",
        "https://drive.google.com/drive/folders/1AfOPRgr5Gl8gll9KfGDEUSYq_Ag7yPJG",
        "적: 망령 대용 (리깅+애니)",
    ),
    (
        "quaternius_medieval_weapons",
        "gdrive",
        "https://drive.google.com/drive/folders/1Z6vYiQxY8W73FXuMWzaTQAg9rzbumnOr",
        "무기: 검·방패·도끼",
    ),
    (
        "quaternius_ultimate_rpg",
        "gdrive",
        "https://drive.google.com/drive/folders/1IhdL3W5XdJrjf_axqAnKvEybMaC3FwF-",
        "소품: 상자·포션·무기",
    ),
    (
        "kenney_impact-sounds",
        "zip",
        "https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip",
        "사운드: 피격(펀치·금속·연질)·발소리",
    ),
    (
        "kenney_rpg-audio",
        "zip",
        "https://kenney.nl/media/pages/assets/rpg-audio/8e99002d76-1677590336/kenney_rpg-audio.zip",
        "사운드: 칼·천·책·문·동전",
    ),
]


def count_files(d: Path) -> int:
    return sum(1 for p in d.rglob("*") if p.is_file())


# 받을 확장자 — Blend/OBJ/glTF 중복본은 제외(용량·쿼터 절약). 텍스처는 FBX 가 참조.
KEEP_EXT = {".fbx", ".png", ".jpg", ".jpeg", ".tga", ".txt", ".md"}


def fetch_gdrive(name: str, url: str) -> None:
    """gdown 은 폴더 목록만 — 파일 본체는 `drive.usercontent.google.com` 직접 요청.
    gdown 의 파일 다운로드 경로(uc?id=)는 익명 접근 몇 번이면 'many accesses' 로 막히는데(2026-09-18 실측),
    usercontent 경로는 같은 시점에도 200 을 준다."""
    import gdown

    out = ROOT / name
    try:
        files = gdown.download_folder(url, output=str(out), skip_download=True, quiet=True)
    except Exception as e:
        print(f"[fetch] {name}: 폴더 목록 실패 — {e!r}")
        return
    files = [f for f in files or [] if Path(f.path).suffix.lower() in KEEP_EXT]
    done = skipped = 0
    for f in files:
        dst = Path(f.local_path)
        if dst.exists() and dst.stat().st_size > 0:
            skipped += 1
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        req = urllib.request.Request(
            f"https://drive.usercontent.google.com/download?id={f.id}&export=download&confirm=t",
            headers={"User-Agent": "Mozilla/5.0"},
        )
        try:
            with urllib.request.urlopen(req, timeout=120) as r, open(dst, "wb") as w:  # nosec B310 — URL 스킴 https 고정
                w.write(r.read())
            done += 1
        except Exception as e:
            print(f"[fetch]   실패 {f.path}: {e!r}")
    print(f"[fetch] {name}: {done} 받음, {skipped} 이미 있음, 대상 {len(files)}")


def fetch_zip(name: str, url: str) -> None:
    z = ROOT / f"{name}.zip"
    out = ROOT / name
    if not z.exists():
        urllib.request.urlretrieve(url, z)  # nosec B310 — url 은 위 ASSETS 표의 하드코딩된 https 상수
    if not out.exists():
        with zipfile.ZipFile(z) as f:
            f.extractall(out)
    print(f"[fetch] {name}: {count_files(out)} files")


if __name__ == "__main__":
    ROOT.mkdir(exist_ok=True)
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    for name, kind, url, _ in ASSETS:
        if only and only not in name:
            continue
        (fetch_gdrive if kind == "gdrive" else fetch_zip)(name, url)

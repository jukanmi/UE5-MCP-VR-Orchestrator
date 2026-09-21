"""
File: build_knowledge.py
Role: NPC RAG 지식 벡터스토어 (재)빌드 CLI.

지식 .md 를 편집한 뒤 이 스크립트로 FAISS 인덱스를 다시 만든다.
런타임(rag_utils.build_vectorstore)도 첫 retrieve 시 자동 빌드하지만,
편집 직후 즉시 반영하거나 일괄 재빌드할 때 사용한다.

지식 구조:
    app/agents/knowledge/<npc>/{lore,persona,history}/*.md

사용 (CognitiveEngine 디렉터리에서):
    python -m app.utils.build_knowledge --all
    python -m app.utils.build_knowledge --agent skadi --force
    python -m app.utils.build_knowledge --list
"""

from __future__ import annotations

import argparse
import os
from typing import List

from .rag_utils import KNOWLEDGE_BASE_PATH, build_vectorstore

# vectorstores 는 출력 폴더이므로 에이전트 후보에서 제외
_EXCLUDE_DIRS = {"vectorstores"}


def discover_agents() -> List[str]:
    """knowledge/ 하위에서 에이전트 폴더명을 수집(vectorstores 제외)."""
    if not os.path.isdir(KNOWLEDGE_BASE_PATH):
        return []
    return sorted(
        d
        for d in os.listdir(KNOWLEDGE_BASE_PATH)
        if os.path.isdir(os.path.join(KNOWLEDGE_BASE_PATH, d)) and d not in _EXCLUDE_DIRS
    )


def _count_md(agent: str) -> int:
    """에이전트 폴더의 .md 문서 수(서브폴더 포함)."""
    root = os.path.join(KNOWLEDGE_BASE_PATH, agent)
    n = 0
    for _dir, _sub, files in os.walk(root):
        n += sum(1 for f in files if f.endswith(".md"))
    return n


def rebuild(agent: str, force: bool = True) -> bool:
    """단일 에이전트 벡터스토어 빌드. 성공 시 True."""
    md = _count_md(agent)
    if md == 0:
        print(f"[Build] {agent}: .md 문서 없음 - 스킵 (knowledge/{agent}/{{lore,persona,history}}/*.md 작성 필요)")
        return False
    vs = build_vectorstore(agent, force_rebuild=force)
    ok = vs is not None
    print(f"[Build] {agent}: {'완료' if ok else '실패'} (문서 {md}개)")
    return ok


def main() -> None:
    parser = argparse.ArgumentParser(description="NPC RAG 지식 벡터스토어 재빌드")
    g = parser.add_mutually_exclusive_group(required=True)
    g.add_argument("--agent", help="특정 NPC id (예: skadi)")
    g.add_argument("--all", action="store_true", help="knowledge/ 의 모든 NPC 빌드")
    g.add_argument("--list", action="store_true", help="에이전트와 문서 수만 출력")
    parser.add_argument(
        "--use-cache",
        action="store_true",
        default=False,
        help="기존 벡터스토어 캐시가 있으면 재사용 (기본 False — CLI 실행 = 항상 재빌드)",
    )
    args = parser.parse_args()

    agents = discover_agents()
    if args.list:
        if not agents:
            print(f"[Build] 에이전트 없음 - {KNOWLEDGE_BASE_PATH} 확인")
            return
        for a in agents:
            print(f"  {a}: {_count_md(a)} md")
        return

    if args.agent is not None and not args.agent.strip():
        print("[Build] 에러: 올바른 에이전트 ID를 입력하세요 (빈 값 불가).")
        return

    targets = agents if args.all else [args.agent]
    if args.agent and args.agent not in agents:
        print(f"[Build] 경고: '{args.agent}' 폴더가 knowledge/ 에 없음 (그래도 시도)")

    built = sum(1 for a in targets if rebuild(a, force=not args.use_cache))
    print(f"[Build] 완료 - {built}/{len(targets)} 빌드 성공")


if __name__ == "__main__":
    main()

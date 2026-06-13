"""
File: rag_utils.py
Purpose: RAG (Retrieval-Augmented Generation) utilities for NPC knowledge bases.
Each NPC has their own vector store for personalized context retrieval.
"""

import os
from typing import Optional, List
from langchain_community.document_loaders import DirectoryLoader, TextLoader
from langchain_text_splitters import RecursiveCharacterTextSplitter
from langchain_community.vectorstores import FAISS

# Try to use new langchain_huggingface, fallback to old if not available
try:
    from langchain_huggingface import HuggingFaceEmbeddings
except ImportError:
    from langchain_community.embeddings import HuggingFaceEmbeddings

# Base paths
KNOWLEDGE_BASE_PATH = "app/agents/knowledge"
VECTORSTORE_PATH = os.path.join(KNOWLEDGE_BASE_PATH, "vectorstores")

# Cache for loaded vector stores
_vectorstore_cache = {}

# chunk_category 로 인정되는 서브폴더명 (PDF 설계서 §5)
_KNOWN_CATEGORIES = {"lore", "persona", "history"}


def get_embeddings():
    """Get the embedding model for vector operations."""
    # 로컬 경로가 있으면 해당 파일을 로드 (오프라인 모드)
    local_path = "app/models/embeddings/all-MiniLM-L6-v2"
    model_id = local_path if os.path.exists(local_path) else "sentence-transformers/all-MiniLM-L6-v2"

    return HuggingFaceEmbeddings(model_name=model_id, model_kwargs={"device": "cpu"})


def build_vectorstore(agent_id: str, force_rebuild: bool = False) -> Optional[FAISS]:
    """
    Build or load a vector store for a specific NPC.

    Args:
        agent_id: NPC identifier (e.g., "Elara", "James")
        force_rebuild: If True, rebuild even if cache exists

    Returns:
        FAISS vector store or None if no knowledge found
    """
    agent_lower = agent_id.lower()
    knowledge_path = os.path.join(KNOWLEDGE_BASE_PATH, agent_lower)
    vectorstore_path = os.path.join(VECTORSTORE_PATH, agent_lower)

    # Check cache first
    if not force_rebuild and agent_lower in _vectorstore_cache:
        return _vectorstore_cache[agent_lower]

    # Try to load existing vectorstore
    if not force_rebuild and os.path.exists(vectorstore_path):
        try:
            embeddings = get_embeddings()
            vectorstore = FAISS.load_local(vectorstore_path, embeddings, allow_dangerous_deserialization=True)
            _vectorstore_cache[agent_lower] = vectorstore
            print(f"[RAG] Loaded existing vectorstore for {agent_id}")
            return vectorstore
        except Exception as e:
            print(f"[RAG] Failed to load vectorstore for {agent_id}: {e}")

    # Build new vectorstore from documents
    if not os.path.exists(knowledge_path):
        print(f"[RAG] No knowledge folder found for {agent_id} at {knowledge_path}")
        return None

    try:
        # Load all documents from the NPC's knowledge folder.
        # 서브폴더 구조: knowledge/<npc>/{lore,persona,history}/*.md (PDF 설계서 §5 chunk_category)
        loader = DirectoryLoader(
            knowledge_path, glob="**/*.md", loader_cls=TextLoader, loader_kwargs={"encoding": "utf-8"}
        )
        documents = loader.load()

        if not documents:
            print(f"[RAG] No documents found for {agent_id}")
            return None

        # 즉시 상위 폴더명(lore/persona/history)을 chunk_category 메타데이터로 태깅.
        # 검색 시 카테고리 라벨로 노출되고, 추후 필터링 확장 지점.
        for doc in documents:
            src = doc.metadata.get("source") or ""
            parent = os.path.basename(os.path.dirname(src)).lower() if src else ""
            doc.metadata["chunk_category"] = parent if parent in _KNOWN_CATEGORIES else "general"
            doc.metadata["npc_id"] = agent_lower

        # Split documents into chunks
        text_splitter = RecursiveCharacterTextSplitter(
            chunk_size=500, chunk_overlap=50, separators=["\n\n", "\n", ".", " "]
        )
        splits = text_splitter.split_documents(documents)

        # Create vector store
        embeddings = get_embeddings()
        vectorstore = FAISS.from_documents(splits, embeddings)

        # Save for future use
        os.makedirs(vectorstore_path, exist_ok=True)
        vectorstore.save_local(vectorstore_path)

        # Cache it
        _vectorstore_cache[agent_lower] = vectorstore

        print(f"[RAG] Built vectorstore for {agent_id} with {len(splits)} chunks")
        return vectorstore

    except Exception as e:
        print(f"[RAG] Error building vectorstore for {agent_id}: {e}")
        return None


def retrieve_context(agent_id: str, query: str, k: int = 3) -> str:
    """
    Retrieve relevant context for an NPC based on a query.

    Args:
        agent_id: NPC identifier
        query: The user's question/statement
        k: Number of relevant chunks to retrieve

    Returns:
        Retrieved context as a formatted string
    """
    vectorstore = build_vectorstore(agent_id)

    if not vectorstore:
        return ""

    try:
        # Retrieve relevant documents
        docs = vectorstore.similarity_search(query, k=k)

        if not docs:
            return ""

        # Format context
        context_parts = []
        for i, doc in enumerate(docs, 1):
            # Safety check: skip if page_content is None
            if not doc.page_content:
                continue

            source = os.path.basename(doc.metadata.get("source") or "unknown")
            category = doc.metadata.get("chunk_category", "general")
            # Clean content and ensure it's a string
            content = str(doc.page_content).strip()
            if content:
                context_parts.append(f"[{category}:{source}] {content}")

        if not context_parts:
            return ""

        context = "\n\n".join(context_parts)
        print(f"[RAG] Retrieved {len(context_parts)} valid chunks for {agent_id}")
        return context

    except Exception as e:
        print(f"[RAG] Error retrieving context for {agent_id}: {e}")
        return ""

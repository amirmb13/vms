"""ArcFace identity matching against enrolled identities.

The AI engine emits 512-d L2-NORMALIZED embeddings, so cosine similarity
reduces to a plain dot product. This brute-force pass is correct for small
watchlists; the scale-up path is:

  TODO(pgvector): enable the `vector` extension, migrate
  Identity.embedding / DetectionRecord.embedding from JSONField to
  pgvector `vector(512)`, add an HNSW index
  (`USING hnsw (embedding vector_cosine_ops)`) and replace `match_identity`
  with `Identity.objects.order_by(CosineDistance("embedding", probe))[:1]`.
"""
from __future__ import annotations

import math

from django.conf import settings

from .models import Identity

EMBEDDING_DIM = 512


def cosine_similarity(a: list[float], b: list[float]) -> float:
    """Cosine similarity; renormalizes defensively in case a caller ever
    passes an un-normalized vector."""
    dot = norm_a = norm_b = 0.0
    for x, y in zip(a, b):
        dot += x * y
        norm_a += x * x
        norm_b += y * y
    if norm_a <= 0.0 or norm_b <= 0.0:
        return 0.0
    return dot / (math.sqrt(norm_a) * math.sqrt(norm_b))


def match_identity(embedding: list[float]) -> tuple[Identity | None, float | None]:
    """Return (best_identity, similarity) or (None, None) below threshold.

    Threshold is `settings.FACE_MATCH_THRESHOLD` (ArcFace cosine; ~0.45 is a
    conservative production default).
    """
    if not isinstance(embedding, list) or len(embedding) != EMBEDDING_DIM:
        return None, None

    threshold: float = getattr(settings, "FACE_MATCH_THRESHOLD", 0.45)
    best: Identity | None = None
    best_sim = -1.0

    for identity in Identity.objects.only("id", "embedding").iterator():
        candidate = identity.embedding
        if not isinstance(candidate, list) or len(candidate) != EMBEDDING_DIM:
            continue
        sim = cosine_similarity(embedding, candidate)
        if sim > best_sim:
            best, best_sim = identity, sim

    if best is not None and best_sim >= threshold:
        return best, best_sim
    return None, None

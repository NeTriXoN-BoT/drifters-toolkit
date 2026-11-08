"""Resolve direct ELF dependencies after applying explicit library replacements."""
from collections import deque
from pathlib import Path


def closure(roots, replacements, needed, resolve, host_library):
    pending = deque(Path(p) for p in roots)
    visited, result = set(), {}
    while pending:
        source = pending.popleft()
        actual = source.resolve(strict=True)
        if actual in visited:
            continue
        visited.add(actual)
        for name in needed(source):
            candidate = Path(replacements[name] if name in replacements else resolve(source, name))
            if candidate.name != name or not candidate.is_file():
                raise RuntimeError('A direct ELF dependency is unresolved: ' + name)
            if not host_library(name):
                prior = result.get(name)
                if prior is not None and prior.resolve(strict=True) != candidate.resolve(strict=True):
                    raise RuntimeError('Conflicting runtime dependency: ' + name)
                result[name] = candidate
            pending.append(candidate)
    return result

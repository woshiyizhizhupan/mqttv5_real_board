from __future__ import annotations

from collections import deque
from datetime import datetime
from threading import Lock


class LogBuffer:
    def __init__(self, max_entries: int = 500) -> None:
        if max_entries <= 0:
            raise ValueError("max_entries must be greater than zero")
        self._entries: deque[str] = deque(maxlen=max_entries)
        self._lock = Lock()

    def append(self, level: str, message: str) -> str:
        timestamp = datetime.now().strftime("%H:%M:%S")
        entry = f"{timestamp} {level.upper()} {message}"
        with self._lock:
            self._entries.append(entry)
        return entry

    def snapshot(self) -> list[str]:
        with self._lock:
            return list(self._entries)

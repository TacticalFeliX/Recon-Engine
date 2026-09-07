"""
ConfigLoader / DatabaseConfig
-------------------------------
Reads simple `key = value` files (.env, config/config.ini-style), with real
process environment variables always taking precedence over file values —
this is what lets a CI pipeline override any setting without touching a
checked-in file. Not a fatal error if the file doesn't exist: all required
values might come from real environment variables instead.
"""
from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Dict, Optional


class ConfigLoader:
    def __init__(self, values: Optional[Dict[str, str]] = None):
        self._values: Dict[str, str] = values or {}

    @classmethod
    def load_from_file(cls, path: str) -> "ConfigLoader":
        values: Dict[str, str] = {}
        try:
            with open(path, "r") as f:
                lines = f.readlines()
        except OSError:
            return cls(values)

        for line in lines:
            trimmed = line.strip()
            if not trimmed or trimmed.startswith("#") or trimmed.startswith(";"):
                continue
            if "=" not in trimmed:
                continue  # silently skip malformed lines; a dev-convenience file, not strictly-validated user input
            key, _, value = trimmed.partition("=")
            key = key.strip()
            value = value.split(";")[0].strip() if ";" in value else value.strip()
            if key:
                values[key] = value
        return cls(values)

    def get(self, key: str, default: str = "") -> str:
        env_val = os.environ.get(key)
        if env_val is not None:
            return env_val
        return self._values.get(key, default)

    def require(self, key: str) -> str:
        env_val = os.environ.get(key)
        if env_val is not None:
            return env_val
        if key in self._values:
            return self._values[key]
        raise RuntimeError(f"ConfigLoader.require: missing required config key '{key}'")


@dataclass
class DatabaseConfig:
    host: str
    port: int
    database: str
    user: str
    password: str

    @classmethod
    def from_config_loader(cls, loader: ConfigLoader) -> "DatabaseConfig":
        return cls(
            host=loader.get("RECON_DB_HOST", "localhost"),
            port=int(loader.get("RECON_DB_PORT", "5432")),
            database=loader.require("RECON_DB_NAME"),
            user=loader.require("RECON_DB_USER"),
            password=loader.get("RECON_DB_PASSWORD", ""),
        )

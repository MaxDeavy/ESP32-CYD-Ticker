"""Lädt server/config.json und baut Seiten-Layout (N Assets pro Seite pro Kategorie)."""

from dataclasses import dataclass
import json
import os
from pathlib import Path
from typing import Dict, List, Optional

CONFIG_PATH = Path(os.getenv("CONFIG_PATH", Path(__file__).resolve().parent / "config.json"))


@dataclass
class AssetDef:
    asset_id: str
    name: str
    category: str
    yahoo: str
    convert_usd_to_eur: bool = False
    yahoo_fallback: str = ""


@dataclass
class AppConfig:
    assets_per_page: int
    categories: Dict[str, str]
    category_order: List[str]
    assets: List[AssetDef]
    pages: List[dict]


def _config_path() -> Path:
    return CONFIG_PATH


def load_config(path: Optional[Path] = None) -> AppConfig:
    p = path or _config_path()
    with open(p, encoding="utf-8") as f:
        raw = json.load(f)

    display = raw.get("display", {})
    categories = raw.get("categories", {})
    category_order = raw.get("category_order", list(categories.keys()))
    assets_per_page = int(display.get("assets_per_page", 2))

    assets: List[AssetDef] = []
    for item in raw.get("assets", []):
        assets.append(
            AssetDef(
                asset_id=item["id"],
                name=item.get("name", item["id"]),
                category=item.get("category", "stock"),
                yahoo=item["yahoo"],
                convert_usd_to_eur=bool(item.get("convert_usd_to_eur", False)),
                yahoo_fallback=item.get("yahoo_fallback", ""),
            )
        )

    pages = build_pages(assets, category_order, categories, assets_per_page)

    return AppConfig(
        assets_per_page=assets_per_page,
        categories=categories,
        category_order=category_order,
        assets=assets,
        pages=pages,
    )


def build_pages(
    assets: List[AssetDef],
    category_order: List[str],
    categories: Dict[str, str],
    assets_per_page: int = 2,
) -> List[dict]:
    pages: List[dict] = []
    per = max(1, assets_per_page)
    for cat in category_order:
        title = categories.get(cat, cat)
        cat_assets = [a for a in assets if a.category == cat]
        for i in range(0, len(cat_assets), per):
            chunk = cat_assets[i : i + per]
            pages.append(
                {
                    "title": title,
                    "category": cat,
                    "ids": [a.asset_id for a in chunk],
                }
            )
    return pages


def config_for_api(cfg: AppConfig) -> dict:
    return {
        "display": {"assets_per_page": cfg.assets_per_page},
        "categories": cfg.categories,
        "category_order": cfg.category_order,
        "pages": cfg.pages,
        "assets": [
            {
                "id": a.asset_id,
                "name": a.name,
                "category": a.category,
                "yahoo": a.yahoo,
            }
            for a in cfg.assets
        ],
    }

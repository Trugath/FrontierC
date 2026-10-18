"""Compile named mining yield and commodity tables to their native layout."""
from pathlib import Path
from gameplay_data import mining_yields, mining_categories
ROOT = Path(__file__).resolve().parents[1]


def resources():
    root = ROOT / 'data/mining'
    yields = mining_yields(root / 'yields.csv')
    categories = mining_categories(root / 'categories.csv')
    return b''.join(value.to_bytes(2, 'little') for value in yields) + bytes(categories)

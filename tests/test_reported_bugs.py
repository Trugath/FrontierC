"""Ship data behind the reported Frontier defects."""
import csv
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReportedShipData(unittest.TestCase):
    def test_imperial_ships_need_their_crews(self):
        path = ROOT / 'data/models/frontier/ship_parameters.csv'
        crews = {}
        with path.open(newline='') as handle:
            for row in csv.DictReader(handle):
                crews[row['name_text']] = int(row['required_crew'])
        self.assertEqual(crews['entry_035_imperial_courier'], 3)
        self.assertEqual(crews['entry_036_imperial_trader'], 6)

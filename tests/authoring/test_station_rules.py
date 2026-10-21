"""Station routing migrations and editable rule validation."""
from pathlib import Path
import tempfile
import unittest

from station_rules import ROOT, HANDLERS, mission_destinations, reply_routes, source


class StationRules(unittest.TestCase):
    def test_original_reply_routes(self):
        groups = {
            'service': '00 01 02 03 04 05 06 07 08 09 18 1b 2a 2d 2e 2f 30 52 68 69 6a 6b 6c',
            'fitting': '0d 6e 6f 70 71 72',
            'yard': '0a 0e 0f 11 12 13 14 15 16 17 51',
            'trade': '0c 28 31',
            'charge': '10 4a 4b 4c 4d 4e 4f 50',
            'talk': '32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e',
            'mission': '19 1a 26 27 29 3f 53 54 55 56 57 58 59 5a 5b 5c 5d 5e 5f 64 65 66 67',
        }
        expected = [HANDLERS.index('board')] * 115
        for handler, indices in groups.items():
            for index in indices.split():
                expected[int(index, 16)] = HANDLERS.index(handler)
        self.assertEqual(reply_routes(), expected)
        self.assertIn('fe2_station_reply_routes[115]', source())

    def test_original_mission_destinations(self):
        self.assertEqual(mission_destinations(), [(0xa92dc602, 13), (0xa925c605, 12), (0xa925c682, 14)])

    def test_reply_edits_and_invalid_references(self):
        text = (ROOT / 'data/interactions/reply_routes.csv').read_text()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'routes.csv'
            path.write_text(text.replace(',service', ',board', 1))
            edited = reply_routes(path)
            self.assertEqual(edited[0], HANDLERS.index('board'))
            self.assertEqual(edited[1:], reply_routes()[1:])
            for invalid in (text.replace(',service', ',unknown', 1),
                            text.replace('reply,handler', 'id,handler'),
                            '\n'.join(text.splitlines()[:-1]),
                            text.replace('text_request_docking_clearance_from_0_traffic_control', 'missing', 1)):
                path.write_text(invalid)
                with self.assertRaises(ValueError):
                    reply_routes(path)

    def test_mission_edits_and_invalid_rules(self):
        text = (ROOT / 'data/interactions/mission_destinations.csv').read_text()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'destinations.csv'
            path.write_text(text.replace(',13', ',15'))
            self.assertEqual(mission_destinations(path)[0], (0xa92dc602, 15))
            for invalid in (text.replace(',13', ',32'), text.replace(',13', ',-1'),
                            text.replace(',13', ',12'),
                            text.replace('keyed_record_012', 'fallback_record_0'),
                            text.replace('keyed_record_012', 'keyed_record_003'),
                            'system_record,mission_flag_bit\n'):
                path.write_text(invalid)
                with self.assertRaises(ValueError):
                    mission_destinations(path)

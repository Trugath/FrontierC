"""Headless gameplay checks.

The game is built once. Each test runs gameplay-check with a scenario name.
The session driver and one file per subject live in tests/gameplay/.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from build_native_intro import build, ROOT
from build_native_viewer import assets
from compiler import windows_executable_inputs, zig

ZIG = zig()


class Gameplay(unittest.TestCase):
    exe = None
    check = None

    @classmethod
    def setUpClass(cls):
        if Gameplay.exe is not None:
            cls.exe = Gameplay.exe
            cls.check = Gameplay.check
            return
        directory = ROOT / 'build/game'
        Gameplay.exe = build(directory, ZIG, game=True)
        assets(directory)
        libraries = [directory / folder / ('libfe2' + part + '.a') for folder, part in
                     [('render', 'render'), ('session', 'session'), ('adlib', 'adlib'),
                      ('position', 'position'), ('contact', 'contact'), ('scene-probe', 'scene_probe')]]
        audio = [ROOT / 'src/engine/host' / name for name in
                 ('intro_audio.c', 'asset_archive.c', 'asset_bind.c', 'game_host.c', 'gameplay.c', 'game_scene.c')]
        audio.append(ROOT / 'src/engine/audio/roland/roland.c')
        audio.extend(ROOT / 'src/engine/audio/speaker' / name for name in
                     ('speaker_player.c', 'speaker_services.c', 'speaker_tables.c'))
        checks = sorted((ROOT / 'tests/gameplay').glob('*.c'))
        libs = ['-lwinmm'] if sys.platform == 'win32' else ['-lasound', '-pthread']
        Gameplay.check = directory / 'gameplay-check.exe'
        subprocess.run([str(ZIG), 'cc', '-std=c99', '-g', '-O0', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-I', str(directory), '-I', str(ROOT / 'src/game'),
                        '-I', str(ROOT / 'src/engine'), '-I', str(ROOT / 'src/engine/host'),
                        '-I', str(ROOT / 'tests'),
                        *map(str, checks), *map(str, audio), *map(str, libraries),
                        *windows_executable_inputs(ZIG, directory), *libs,
                        '-o', str(Gameplay.check)], check=True)
        cls.exe = Gameplay.exe
        cls.check = Gameplay.check

    def scenario(self, selection, mode=None, timeout=60, cwd=None, extra=()):
        command = [str(self.check), str(selection)]
        if mode:
            command.append(mode)
        command.extend(extra)
        result = subprocess.run(command, text=True, capture_output=True, timeout=timeout, cwd=cwd)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result


class FlightChecks(Gameplay):
    def test_flight_and_starting_configurations(self):
        for selection in (1, 0x101, 0x201, 0x401, 0x501):
            with self.subTest(selection=hex(selection)):
                result = self.scenario(selection)
                self.assertIn('Population:', result.stdout)

    def test_repeated_intro_then_flight(self):
        self.scenario(1, 'intro')

    def test_offence_keeps_the_view(self):
        result = self.scenario(0x101, 'view')
        self.assertIn('Launch clearance:', result.stdout)
        self.assertIn('Left without permission:', result.stdout)

    def test_firing_and_comms(self):
        result = self.scenario(1, 'fire')
        self.assertIn('beams', result.stdout)
        self.assertIn('Comms wrap', result.stdout)

    def test_scanner_marker_provider(self):
        result = self.scenario(1, 'scanner')
        self.assertIn('Scanner: stems, caps, colour, clipping and preservation passed', result.stdout)

    def test_pointer_launch_and_comms(self):
        result = self.scenario(1, 'hud')
        self.assertIn('HUD icon pixel 44', result.stdout)
        self.assertIn('Secondary steer', result.stdout)

    def test_space_dust_while_flying_straight(self):
        result = self.scenario(1, 'dust')
        self.assertIn('Space dust:', result.stdout)

    def test_views_with_held_turn_keys(self):
        self.scenario(1, 'views')

    def test_planet_impact_crashes(self):
        result = self.scenario(1, 'crash')
        self.assertIn('slow landing kept hull', result.stdout)
        self.assertIn('fast impact explosions', result.stdout)
        self.assertIn('Wreck smoke', result.stdout)


class StationChecks(Gameplay):
    def test_station_screens(self):
        result = self.scenario(1, 'station', timeout=120)
        for phase in ('Station portrait:', 'Market', 'Bought', 'Sold', 'Equipment fitted', 'Deluxe service',
                      'Service:', 'Bulletin board', 'Advert', 'Fitting', 'Ships for sale', 'Launch',
                      'Lift-off', 'Text failures: 0'):
            self.assertIn(phase, result.stdout)

    def test_shipyard_purchase(self):
        result = self.scenario(1, 'shipyard', timeout=120)
        for phase in ('Ship offer', 'Ship preview:', 'Ship bought', 'Launch with the new ship'):
            self.assertIn(phase, result.stdout)

    def test_station_mission(self):
        result = self.scenario(0x201, 'mission', timeout=120)
        self.assertIn('Mission offer', result.stdout)
        self.assertIn('Mission taken', result.stdout)

    def test_gameplay_loops(self):
        result = self.scenario(0x201, 'loops', timeout=180)
        for phase in ('Trading:', 'Bought', 'Sold',
                      'Mission requests keep a stale selector high word',
                      'Mission taken',
                      'Heat shift wraps',
                      'Lift-off:',
                      'Mining compaction',
                      'Mining hopper signed compare'):
            self.assertIn(phase, result.stdout)

    def test_parcel_delivery(self):
        result = self.scenario(1, 'deliver', timeout=180)
        self.assertIn('Parcel delivered:', result.stdout)

    def test_status_spawn_clears_the_source(self):
        result = self.scenario(0x101, 'spawn')
        self.assertIn('Canister ', result.stdout)
        self.assertIn('Company:', result.stdout)

    def test_police_launch(self):
        result = self.scenario(1, 'police')
        self.assertIn('Police launch', result.stdout)
        self.assertIn('Police patrol', result.stdout)
        for phase in ('Interception', 'Escort', 'Hold', 'Leaving', 'Removal'):
            self.assertIn(phase, result.stdout)

    def test_police_launch_from_each_station(self):
        result = self.scenario(0x101, 'ports')
        for port in ('rotating shape 1', 'rotating shape 3', 'rotating other',
                     'rotating shape 0', 'fixed', 'free'):
            self.assertRegex(result.stdout, r'(?m)^Port ' + re.escape(port) + r' \([^)]+\): ')
        self.assertIn('flying:', result.stdout)

    def test_lave_departure(self):
        result = self.scenario(0x201, 'lave')
        self.assertIn('Lave departure:', result.stdout)


class NavigationChecks(Gameplay):
    def test_orbital_navigation(self):
        for selection in (1, 0x101, 0x201, 0x401, 0x501):
            with self.subTest(selection=hex(selection)):
                result = self.scenario(selection, 'navigation')
                self.assertIn('Navigation controls and return to flight passed', result.stdout)

    def test_hyperspace_jump(self):
        result = self.scenario(1, 'jump', timeout=180)
        for phase in ('Departure', 'Target: BD 4523', 'Arrival: BD 4523'):
            self.assertIn(phase, result.stdout)

    def test_map_and_phone(self):
        result = self.scenario(1, 'screens')
        self.assertIn('Galactic map:', result.stdout)
        self.assertIn('Commander status:', result.stdout)
        self.assertIn('Status preview:', result.stdout)
        self.assertIn('System details:', result.stdout)
        self.assertIn('Trade details:', result.stdout)
        self.assertIn('Government details rendered', result.stdout)
        self.assertIn('Phone docked:', result.stdout)
        self.assertIn('Phone flight:', result.stdout)
        self.assertIn('Icon clicks:', result.stdout)
        self.assertNotIn('Text expansion failed 98', result.stderr)


class CommanderChecks(Gameplay):
    def test_commander_files(self):
        with tempfile.TemporaryDirectory(prefix='fe2-commander-') as directory:
            result = self.scenario(1, 'commander', timeout=180, cwd=directory)
        for phase in ('Save dialog:', 'Saved: JAMESON._01', 'Loaded: restarts 1', 'position restored', 'Image again',
                      'Saved again', 'Flying on', 'Opaque commander bytes preserved',
                      'Save overwrite dismissed', 'Service counters restored'):
            self.assertIn(phase, result.stdout)
        original = os.environ.get('FE2_ORIGINAL_COMMANDER')
        if original:
            with tempfile.TemporaryDirectory(prefix='fe2-original-commander-') as directory:
                local = Path(directory) / 'ORIGINAL._01'
                shutil.copyfile(Path(original).resolve(), local)
                extra = [str(local)]
                exported = os.environ.get('FE2_NATIVE_COMMANDER_OUTPUT')
                if exported:
                    extra.append(str(Path(exported).resolve()))
                result = self.scenario(1, 'original', timeout=180, cwd=directory, extra=extra)
            self.assertIn('Original commander continued for 120 native frames', result.stdout)

    def test_ship_information_all_starts(self):
        for selection in (1, 0x101, 0x201, 0x401, 0x501):
            with self.subTest(selection=hex(selection)):
                result = self.scenario(selection, 'shipinfo')
                self.assertEqual(result.stdout.count('Ship information page'), 8)
                self.assertIn('Refuelled:', result.stdout)
                self.assertIn('Engines restored', result.stdout)
                self.assertIn('Customs:', result.stdout)

    def test_preferences_pointer_controls(self):
        result = self.scenario(1, 'ui')
        self.assertIn('UI: preferences open', result.stdout)
        self.assertIn('Preferences: backdrop', result.stdout)


class SceneChecks(Gameplay):
    def test_scene_orchestration(self):
        result = self.scenario(1, 'scene')
        self.assertIn('four attachment branches and frame model passed', result.stdout)

    def test_far_object_projection(self):
        result = self.scenario(1, 'far')
        self.assertIn('stale projection invalidation passed', result.stdout)

    def test_exit_scene_to_intro(self):
        result = self.scenario(1, 'exit')
        self.assertIn('returned to intro', result.stdout)


class GameLaunch(Gameplay):
    def test_intro_to_game(self):
        result = subprocess.run([str(self.exe), '--frames', '120'], text=True,
                                capture_output=True, timeout=60, check=True)
        self.assertIn('started selection=1', result.stdout)
        self.assertIn('frames=120 errors=0', result.stdout)
        # Highest detail preset (1FD1h = 1, 1FD3h = 2) submits the nearer bodies.
        self.assertIn('game=1 bodies=7 scenes=110 submitted=654', result.stdout)

    @unittest.skipUnless(sys.platform == 'win32' and os.environ.get('FE2_WINDOW_TESTS') == '1',
                         'Set FE2_WINDOW_TESTS=1 to test a visible Win32 window')
    def test_window_flight_input(self):
        import ctypes
        from ctypes import wintypes
        from win32_test_window import find_window
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.PostMessageA.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        user.PostMessageA.restype = wintypes.BOOL
        process = subprocess.Popen([str(self.exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 20
            window = None
            while process.poll() is None and time.monotonic() < deadline:
                window = find_window(process.pid, b'FE2NativeGame')
                if window:
                    break
                time.sleep(0.05)
            self.assertTrue(window, 'Game window did not open')
            time.sleep(1)
            self.assertTrue(user.PostMessageA(window, 0x102, ord('1'), 0))
            time.sleep(1)
            # Pass original set-1 scan codes, like physical Win32 key messages.
            self.assertTrue(user.PostMessageA(window, 0x100, 0x76, 0x41 << 16))
            self.assertTrue(user.PostMessageA(window, 0x101, 0x76, (0x41 << 16) | (3 << 30)))
            time.sleep(0.25)  # Let the controller consume its single-key latch.
            self.assertTrue(user.PostMessageA(window, 0x100, 0x0d, 0x1c << 16))
            time.sleep(2)
            self.assertTrue(user.PostMessageA(window, 0x101, 0x0d, (0x1c << 16) | (3 << 30)))
            # Escape belongs to the native pause/preferences flow.
            self.assertTrue(user.PostMessageA(window, 0x100, 0x1b, 0x01 << 16))
            self.assertTrue(user.PostMessageA(window, 0x101, 0x1b, (0x01 << 16) | (3 << 30)))
            time.sleep(0.5)
            self.assertIsNone(process.poll(), 'Escape closed the game instead of pausing')
            self.assertTrue(user.PostMessageA(window, 0x100, 0x1b, 0x01 << 16))
            self.assertTrue(user.PostMessageA(window, 0x101, 0x1b, (0x01 << 16) | (3 << 30)))
            time.sleep(0.5)
            self.assertIsNone(process.poll(), 'Opening preferences closed the game')
            self.assertTrue(user.PostMessageA(window, 0x10, 0, 0))  # WM_CLOSE
            stdout, stderr = process.communicate(timeout=20)
            self.assertEqual(process.returncode, 0, stdout + stderr)
            self.assertIn('started selection=1', stdout)
            self.assertIn('errors=0', stdout)
            self.assertIn('game=1 bodies=7', stdout)
            self.assertNotIn('flight mode=60', stdout)
            self.assertNotIn('throttle=0\n', stdout)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()


if __name__ == '__main__':
    unittest.main()

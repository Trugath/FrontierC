"""Build the native Win32 or Linux intro host (intro, not the full game)."""
from compiler import windows_executable_inputs, zig
import argparse
import subprocess
import sys
import shutil
import json
from pathlib import Path

from build_c_adlib import build_library as build_adlib
from build_c_frontier import ROOT, build_library
from build_native_viewer import assets
from frontier_data import records_from_assets, write_archive


def build(directory, compiler, game=False):
    directory = directory.resolve()
    compiler = compiler.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    import optional_tables_c
    optional_tables_c.speaker(directory / 'speaker_tables.h')
    shutil.copyfile(ROOT / 'LICENSE', directory / 'LICENSE.txt')
    if (ROOT / 'data' / 'video' / 'font.bdf').is_file():
        write_archive(records_from_assets(assets(directory)), directory / 'frontier.dat')
    else:
        print('frontier.dat was not packed. Unpack a Frontier install, then pack it with frontier-data.')
    render = build_library(directory / 'render', compiler, 'render', None)
    session = build_library(directory / 'session', compiler, 'session', None)
    adlib = build_adlib(directory / 'adlib', compiler)
    position = build_library(directory / 'position', compiler, 'position', None)
    contact = build_library(directory / 'contact', compiler, 'contact', None)
    probe = build_library(directory / 'scene-probe', compiler, 'scene_probe', None)
    windows = sys.platform == 'win32'
    win_inputs = windows_executable_inputs(compiler, directory) if windows else []
    exe = directory / (('frontier' if game else 'frontier-intro') + ('.exe' if windows else ''))
    subprocess.run([str(compiler), 'cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-I', str(directory), '-I', str(ROOT / 'src/game'), '-I', str(ROOT / 'src/engine'),
        '-I', str(ROOT / 'src/engine/host'),
        '-DFE2_NATIVE_GAME=' + str(int(game)),
        str(ROOT / 'src/engine/host/intro_main.c'),
        str(ROOT / 'src/engine/host/intro_audio.c'),
        str(ROOT / 'src/engine/audio/roland/roland.c'),
        str(ROOT / 'src/engine/audio/speaker/speaker_player.c'),
        str(ROOT / 'src/engine/audio/speaker/speaker_services.c'),
        str(ROOT / 'src/engine/audio/speaker/speaker_tables.c'),
        str(ROOT / 'src/engine/host/game_host.c'),
        str(ROOT / 'src/engine/host/gameplay.c'),
        str(ROOT / 'src/engine/host/game_scene.c'),
        str(ROOT / 'src/engine/host/asset_archive.c'),
        str(ROOT / 'src/engine/host/asset_bind.c'),
        *win_inputs,
        str(render), str(session), str(adlib), str(position), str(contact), str(probe),
        *(['-lgdi32', '-luser32', '-lwinmm'] if windows else ['-lX11', '-lasound', '-pthread']),
        '-o', str(exe)], check=True)
    data_exe = directory / ('frontier-data' + ('.exe' if windows else ''))
    data_names = ('data_tool.c', 'asset_archive.c', 'asset_files.c', 'asset_ini.c', 'asset_image.c',
                  'inflate.c', 'sha256.c', 'asset_program.c', 'asset_decode.c', 'asset_model.c',
                  'asset_tree.c', 'asset_unpack.c', 'asset_adlib.c', 'asset_tables.c')
    seed = directory / 'authored_seed.c'
    subprocess.run([sys.executable, str(ROOT / 'tools' / 'embed_authored.py'), str(seed)], check=True)
    subprocess.run([str(compiler), 'cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-I', str(ROOT / 'src/engine/host'),
        *[(str(ROOT / 'src/engine/host' / name)) for name in data_names],
        *win_inputs,
        str(seed), '-o', str(data_exe)], check=True)
    sources = [ROOT / 'src/engine/host' / name for name in
               ('intro_main.c', 'intro_audio.c', 'game_host.c', 'gameplay.c', 'game_scene.c',
                'asset_archive.c', 'asset_bind.c', *data_names)]
    sources.append(ROOT / 'src/engine/audio/roland/roland.c')
    sources.extend(ROOT / 'src/engine/audio/speaker' / name for name in
                   ('speaker_player.c', 'speaker_services.c', 'speaker_tables.c'))
    sources.extend(Path(path) for path in win_inputs if str(path).endswith('.c'))
    (directory / 'host-manifest.json').write_text(json.dumps({
        'sources': [str(path.relative_to(ROOT)) for path in sources],
        'executable': str(exe),
        'data_executable': str(data_exe),
    }, indent=2) + '\n', encoding='utf-8')
    return exe


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=ROOT / 'build/native-intro')
    p.add_argument('--zig', type=Path, default=zig())
    p.add_argument('--game', action='store_true')
    a = p.parse_args()
    print(build(a.output, a.zig, game=a.game))

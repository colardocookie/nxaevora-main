# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""tools/run_game.py: the game it finds by its name, in recompiled/ where the driver puts it, else in the games/ an
older version left."""

import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXT = '.exe' if sys.platform == 'win32' else ''


def load(data):
    """run_game.py with data as the user's data folder, which it reads once loaded"""
    os.environ['APPDATA' if sys.platform == 'win32' else 'XDG_DATA_HOME'] = str(data)
    spec = importlib.util.spec_from_file_location('run_game', ROOT / 'tools/run_game.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FindGame(unittest.TestCase):
    def setUp(self):
        if sys.platform == 'darwin':
            self.skipTest('the data folder is ~/Library/Application Support there, not one a variable names')
        self.home = tempfile.TemporaryDirectory()
        self.data = Path(self.home.name) / 'scarletnx'
        self.run_game = load(self.home.name)

    def tearDown(self):
        self.home.cleanup()

    def program(self, *parts):
        path = self.data.joinpath(*parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('')
        return str(path)

    def test_recompiled(self):
        made = self.program('recompiled', 'G', 'G' + EXT)
        self.program('games', 'G', 'G' + EXT)  # an older version's copy: the one in recompiled/ is played
        self.assertEqual(self.run_game.find_game('G'), made)

    def test_older_games(self):
        made = self.program('games', 'G', 'G' + EXT)
        self.assertEqual(self.run_game.find_game('G'), made)
        loose = self.program('games', 'L' + EXT)
        self.assertEqual(self.run_game.find_game('L'), loose)

    def test_missing(self):
        with self.assertRaises(SystemExit) as said:
            self.run_game.find_game('G')
        now = os.path.join(str(self.data), 'recompiled', 'G', 'G' + EXT)
        old = os.path.join(str(self.data), 'games', 'G', 'G' + EXT)
        self.assertEqual(
            str(said.exception), 'neither %s nor %s exists: recompile the game (scarletnx -r)' % (now, old)
        )


if __name__ == '__main__':
    unittest.main()

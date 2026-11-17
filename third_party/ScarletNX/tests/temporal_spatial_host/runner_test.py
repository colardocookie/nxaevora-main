# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Check the real runner's executable status handling without a compiler or Vulkan."""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='spatial-runner-') as temporary:
    root = Path(temporary)
    compiler = root / 'compiler.py'
    compiler.write_text('''#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
import os
from pathlib import Path
import sys
out = Path(sys.argv[sys.argv.index('-o') + 1])
status = 42 if out.name == os.environ['FAIL_CASE'] else 0
out.write_text('#!/bin/sh\\necho "case ' + out.name + ' status ' + str(status) + '"\\nexit ' + str(status) + '\\n')
out.chmod(0o755)
''')
    compiler.chmod(0o755)
    for failure in ('none', 'readability', 'refusal', 'fallback', 'render_pair'):
        build = root / failure
        build.mkdir()
        env = dict(os.environ, CLANG=str(compiler), FAIL_CASE=failure)
        result = subprocess.run(['sh', str(here / 'run.sh'), str(build)], env=env,
                                text=True, capture_output=True)
        expected = 0 if failure == 'none' else 42
        assert result.returncode == expected, (failure, result.returncode, result.stdout, result.stderr)
        assert ('temporal_spatial_host: ok' in result.stdout) == (expected == 0)
        if expected:
            assert f'case {failure} status 42' in result.stderr
print('spatial runner: success control and all four executable failures propagated')

"""
Pré-script do ambiente `native`: gera .pio/storage_test.bin com o catálogo
de teste (test/fixtures/catalog) antes de compilar e rodar os testes.
"""

Import("env")  # noqa: F821  (injetado pelo PlatformIO)

import subprocess
import sys
from pathlib import Path

project_root = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
subprocess.run([sys.executable, str(project_root / "tools" / "storage-packer" / "pack_storage.py"),
                "-c", str(project_root / "test" / "fixtures" / "catalog"),
                "-o", str(project_root / ".pio" / "storage_test.bin")],
               check=True, stdout=subprocess.DEVNULL)

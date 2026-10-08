"""
Pré-script do ambiente `native`: gera .pio/storage_teste.bin com o catálogo
de teste (test/fixtures/catalog) antes de compilar e rodar os testes.
"""

Import("env")  # noqa: F821  (injetado pelo PlatformIO)

import subprocess
import sys
from pathlib import Path

raiz = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
subprocess.run([sys.executable, str(raiz / "tools" / "storage-packer" / "pack_storage.py"),
                "-c", str(raiz / "test" / "fixtures" / "catalog"),
                "-o", str(raiz / ".pio" / "storage_teste.bin")],
               check=True, stdout=subprocess.DEVNULL)

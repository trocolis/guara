"""
Alvo extra do PlatformIO: gera o storage.bin e grava só a partição 'storage'.

    pio run -e esp32 -t upload_storage

Não toca no firmware nem nas partições 'save' e 'nvs' (o Guará é preservado).
"""

Import("env")  # noqa: F821  (injetado pelo PlatformIO)

import subprocess
import sys
from pathlib import Path

RAIZ = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
IMAGEM = RAIZ / ".pio" / "storage.bin"
OFFSET_STORAGE = "0x2F0000"


def empacotar(*args, **kwargs):
    resultado = subprocess.run([sys.executable, str(RAIZ / "tools" / "storage-packer" / "pack_storage.py"),
                                "-o", str(IMAGEM)])
    return resultado.returncode


def gravar(*args, **kwargs):
    if empacotar():
        return 1
    env.AutodetectUploadPort()  # noqa: F821
    comando = [
        env.subst("$PYTHONEXE"),  # noqa: F821
        env.subst("$UPLOADER"),  # noqa: F821
        "--chip", "esp32",
        "--port", env.subst("$UPLOAD_PORT"),  # noqa: F821
        "--baud", env.subst("$UPLOAD_SPEED"),  # noqa: F821
        "write_flash", OFFSET_STORAGE, str(IMAGEM),
    ]
    print(" ".join(comando))
    return subprocess.run(comando).returncode


env.AddCustomTarget(  # noqa: F821
    name="build_storage",
    dependencies=None,
    actions=[empacotar],
    title="Gerar storage.bin",
    description="Empacota VROM e catálogo em .pio/storage.bin",
)
env.AddCustomTarget(  # noqa: F821
    name="upload_storage",
    dependencies=None,
    actions=[gravar],
    title="Gravar storage",
    description="Grava .pio/storage.bin no offset da partição storage",
)

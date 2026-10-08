"""
Alvo extra do PlatformIO: gera o storage.bin e grava só a partição 'storage'.

    pio run -e esp32 -t build_storage     # só gera .pio/storage.bin
    pio run -e esp32 -t upload_storage    # gera e grava

Não toca no firmware nem nas partições 'save' e 'nvs' (o Guará é preservado).
"""

Import("env")  # noqa: F821  (injetado pelo PlatformIO)

import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
IMAGE_PATH = PROJECT_ROOT / ".pio" / "storage.bin"
# Offset da partição 'storage' em partitions.csv
STORAGE_OFFSET = "0x2F0000"


def build_storage(*args, **kwargs):
    """Executa o storage-packer; retorna o código de saída (0 = sucesso)."""
    packer = PROJECT_ROOT / "tools" / "storage-packer" / "pack_storage.py"
    return subprocess.run([sys.executable, str(packer), "-o", str(IMAGE_PATH)]).returncode


def upload_storage(*args, **kwargs):
    """Gera a imagem e grava no offset da partição com o esptool do PlatformIO."""
    if build_storage():
        return 1
    env.AutodetectUploadPort()  # noqa: F821
    command = [
        env.subst("$PYTHONEXE"),  # noqa: F821
        env.subst("$UPLOADER"),  # noqa: F821
        "--chip", "esp32",
        "--port", env.subst("$UPLOAD_PORT"),  # noqa: F821
        "--baud", env.subst("$UPLOAD_SPEED"),  # noqa: F821
        "write_flash", STORAGE_OFFSET, str(IMAGE_PATH),
    ]
    print(" ".join(command))
    return subprocess.run(command).returncode


env.AddCustomTarget(  # noqa: F821
    name="build_storage",
    dependencies=None,
    actions=[build_storage],
    title="Gerar storage.bin",
    description="Empacota VROM e catálogo em .pio/storage.bin",
)
env.AddCustomTarget(  # noqa: F821
    name="upload_storage",
    dependencies=None,
    actions=[upload_storage],
    title="Gravar storage",
    description="Grava .pio/storage.bin no offset da partição storage",
)

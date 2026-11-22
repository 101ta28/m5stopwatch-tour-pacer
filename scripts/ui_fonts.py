"""Locate the same Noto CJK typefaces for previews and baked watch fonts."""
import os
from pathlib import Path
from PIL import ImageFont


def noto_font(size, weight):
    filename = f"NotoSansCJK-{weight}.ttc"
    directory = os.environ.get("NOTO_CJK_DIR")
    try:
        # With a bare filename Pillow searches the system font directories.
        return ImageFont.truetype(str(Path(directory) / filename) if directory else filename, size, index=0)
    except OSError as error:
        raise SystemExit(f"{filename} が見つかりません。NOTO_CJK_DIR にフォントのフォルダーを指定してください。") from error

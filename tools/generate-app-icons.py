"""Resize the approved vector-derived master; no product or package IDs change."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1] / 'apps/mobile'
with Image.open(root / 'branding/app-icon.png') as source:
    image = source.convert('RGBA')
    image.save(root / 'windows/runner/resources/app_icon.ico', sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    for density, size in [('mdpi',48),('hdpi',72),('xhdpi',96),('xxhdpi',144),('xxxhdpi',192)]:
        image.resize((size,size), Image.Resampling.LANCZOS).save(root / f'android/app/src/main/res/mipmap-{density}/ic_launcher.png')

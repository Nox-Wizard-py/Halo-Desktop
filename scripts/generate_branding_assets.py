import os
from PIL import Image

def generate_assets():
    source_path = r'C:/Users/hossa/.gemini/antigravity/brain/4ae8c152-9180-455e-92d7-608a09bb7a92/.user_uploaded/media_1794402303094_701331c5.jpg'
    output_dir = os.path.abspath('resources/icons')
    os.makedirs(output_dir, exist_ok=True)

    img = Image.open(source_path).convert('RGB')
    w, h = img.size

    # Clean up bottom-right Gemini watermark (x >= 920, y >= 920) by zeroing it to black (0, 0, 0)
    cleaned_orig = img.copy()
    orig_pixels = cleaned_orig.load()
    for y in range(920, h):
        for x in range(920, w):
            orig_pixels[x, y] = (0, 0, 0)

    # 1. High-resolution source PNG with original background preserved as it is
    orig_png_path = os.path.join(output_dir, 'halo_original.png')
    cleaned_orig.save(orig_png_path, 'PNG')
    print(f'Saved: {orig_png_path}')

    # Also save as halo_1024.png for convenience
    p1024 = os.path.join(output_dir, 'halo_1024.png')
    cleaned_orig.save(p1024, 'PNG')
    print(f'Saved: {p1024}')

    # 2. Resized PNGs with background intact
    for s in [256, 64, 32, 16]:
        resized = cleaned_orig.resize((s, s), Image.Resampling.LANCZOS)
        p = os.path.join(output_dir, f'halo_{s}.png')
        resized.save(p, 'PNG')
        print(f'Saved: {p}')

    # 3. Windows Application Icon (halo.ico) - full multi-resolution mipmaps with background intact
    app_ico_sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
    app_ico_path = os.path.join(output_dir, 'halo.ico')
    cleaned_orig.save(app_ico_path, format='ICO', sizes=app_ico_sizes)
    print(f'Saved: {app_ico_path} with {len(app_ico_sizes)} resolutions')

    # 4. System Tray Icon (halo_tray.ico) - notification area sizes with background intact
    tray_ico_sizes = [(48, 48), (32, 32), (24, 24), (20, 20), (16, 16)]
    tray_ico_path = os.path.join(output_dir, 'halo_tray.ico')
    cleaned_orig.save(tray_ico_path, format='ICO', sizes=tray_ico_sizes)
    print(f'Saved: {tray_ico_path} with {len(tray_ico_sizes)} resolutions')

    # Remove halo_transparent.png if it exists
    trans_p = os.path.join(output_dir, 'halo_transparent.png')
    if os.path.exists(trans_p):
        os.remove(trans_p)
        print(f'Removed: {trans_p}')

if __name__ == '__main__':
    generate_assets()

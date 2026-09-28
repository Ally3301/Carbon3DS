#!/usr/bin/env python3
"""Prepare runtime RomFS from recovered Carbon assets.

Vehicle geometry uses N3P1, one file per customization component.  Car-local
and global SHPM textures are kept as rebuildable PNG assets and converted to T3X
with devkitPro's tex3ds for the 3DS runtime.

Global material ids recovered from the original game:
  1000 CARBONFIBRE   1001 CarBottom   1002 DRIVER
  1003 MESH          1004 Carbon_red_legend
  1500 PAINT         1501 WINDOW
  1990 DUMMY_WHEEL   1995 TIRE_BACK   1996 CALIPER

1500/PAINT is *not* packed as a runtime T3X.  The original texture is a flat
red modulation source and the actual body colour is selected dynamically.
The renderer therefore treats material 1500 as a paint shader class.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "assets" / "generated" / "3ds"
NORMALIZED = ROOT / "assets" / "normalized"
VEHICLES = GENERATED / "vehicles_psp" if (GENERATED / "vehicles_psp").is_dir() else GENERATED / "vehicles"
GLOBAL = GENERATED / "vehicle_global"
DEST = ROOT / "romfs"
TEX = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro")) / "tools/bin/tex3ds"

# These archives are intact in RomFS for traffic/police spawning, but never
# enter the player catalogue.  The *2 pursuit variants notably contain the
# incomplete material path recovered from CopCarRenderInfo.
NPC_ONLY_VEHICLES = frozenset({
    "CUBEVAN", "LARGESUV", "MAZDA3", "PICKUP", "SEDAN", "SMALLSUV", "TAXI",
    "PURSUITMUSCLE1", "PURSUITMUSCLE2", "PURSUITSEDAN1", "PURSUITSEDAN2",
    "PURSUITSUV1", "PURSUITSUV2", "PURSUITWAGON1", "PURSUITWAGON2",
})


def fresh(dst: Path, src: Path):
    return dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime


def copy(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not fresh(dst, src):
        shutil.copy2(src, dst)


def convert_png(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if fresh(dst, src):
        return
    if not TEX.exists():
        raise SystemExit(
            f"tex3ds not found: {TEX}\n"
            "Install/use the devkitPro 3DS toolchain or set DEVKITPRO."
        )
    subprocess.run(
        [str(TEX), "-f", "rgba", "-z", "auto", "-o", str(dst), str(src)],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def convert_psp_png(src: Path, dst: Path):
    """Convert correctly decoded PSP RGBA assets without channel heuristics.

    CLUT4 now arrives from ``psp_texture_extract.py`` as RGBA4444-expanded
    RGBA, and CLUT8 is RGBA8888.  Applying the former BGR swap, grayscale
    badges, or synthetic lens colours here would undo that source correction.
    Wheel maps retain their dedicated border-alpha treatment because their
    black atlas rectangle is not a vehicle-local CLUT colour issue.
    """
    from PIL import Image, ImageOps
    fixed = GENERATED / "psp_rgba_for_t3x" / src.name
    fixed.parent.mkdir(parents=True, exist_ok=True)
    if not fresh(fixed, src):
        image = Image.open(src).convert("RGBA")
        if "wheels_psp" in src.parts:
            from collections import deque
            r, g, b, a = image.split()
            level = ImageOps.grayscale(Image.merge("RGB", (r, g, b)))
            out = Image.merge("RGBA", (level, level, level, a))
            px = out.load(); w, h = out.size; queue = deque(); seen = set()
            for x in range(w): queue.extend(((x, 0), (x, h - 1)))
            for y in range(h): queue.extend(((0, y), (w - 1, y)))
            while queue:
                x, y = queue.popleft()
                if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
                    continue
                rr, gg, bb, aa = px[x, y]
                if max(rr, gg, bb) > 28:
                    continue
                seen.add((x, y)); px[x, y] = (rr, gg, bb, 0)
                queue.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
            out.save(fixed)
        else:
            label = src.stem.upper()
            r, g, b, a = image.split()
            level = ImageOps.grayscale(Image.merge("RGB", (r, g, b)))
            if "BADGING" in label or "BADGE" in label:
                # Vehicle badges are coverage + material tint in the PSP
                # renderer. Neutral luminance preserves their engraving/logo
                # without treating mask palette colours as literal neon RGB.
                image = Image.merge("RGBA", (level, level, level, a))
            elif "HEADLIGHT" in label:
                # Lens palette is an intensity mask; reproduce the PSP's
                # warm-white lens response while keeping recovered coverage.
                warm_g = level.point(lambda x: (x * 230) // 255)
                warm_b = level.point(lambda x: (x * 184) // 255)
                image = Image.merge("RGBA", (level, warm_g, warm_b, a))
            elif "TAILLIGHT" in label or "BRAKELIGHT" in label:
                # Rear lenses are red material masks, not direct palette RGB.
                red = level.point(lambda x: (x * 185) // 255)
                image = Image.merge("RGBA", (red, red.point(lambda x: x // 9), red.point(lambda x: x // 16), a))
            image.save(fixed)
    convert_png(fixed, dst)

def prepare_main_menu_asset(name):
    """Fit one generated menu layer into a power-of-two PICA texture."""
    from PIL import Image
    src = ROOT / "assets" / "generated" / f"{name}_ai.png"
    out = GENERATED / f"{name}_3ds.png"
    if not src.exists():
        return None
    if not fresh(out, src):
        image=Image.open(src).convert("RGBA")
        if name == "hud_nitro_fill":
            box=image.getbbox()
            if box: image=image.crop(box)
            image.thumbnail((124,28), Image.Resampling.LANCZOS)
            canvas=Image.new("RGBA", (128,32), (0,0,0,0))
            canvas.alpha_composite(image, ((128-image.width)//2, (32-image.height)//2))
        elif name == "hud_gauge_needle":
            box=image.getbbox()
            if box: image=image.crop(box)
            image.thumbnail((28,112), Image.Resampling.LANCZOS)
            canvas=Image.new("RGBA", (256,256), (0,0,0,0))
            # Generated needle points upward; bottom-center is its hub/pivot.
            canvas.alpha_composite(image, ((256-image.width)//2, 128-image.height))
        elif name == "menu_selection_tight":
            # Some generators encode transparent surroundings as solid black.
            # Remove only the dark region connected to the border, then crop
            # the remaining plate to its real visual bounds.
            from collections import deque
            px=image.load(); w,h=image.size; q=deque(); seen=set()
            for x in range(w): q.extend(((x,0),(x,h-1)))
            for y in range(h): q.extend(((0,y),(w-1,y)))
            while q:
                x,y=q.popleft()
                if (x,y) in seen or not (0<=x<w and 0<=y<h): continue
                r,g,b,a=px[x,y]
                if a == 0 or (r < 18 and g < 18 and b < 18):
                    seen.add((x,y)); px[x,y]=(r,g,b,0)
                    q.extend(((x+1,y),(x-1,y),(x,y+1),(x,y-1)))
            box=image.getbbox()
            if box: image=image.crop(box)
            image.thumbnail((252,60), Image.Resampling.LANCZOS)
            canvas=Image.new("RGBA", (256,64), (0,0,0,0))
            canvas.alpha_composite(image, ((256-image.width)//2, (64-image.height)//2))
        else:
            image.thumbnail((512,384), Image.Resampling.LANCZOS)
            canvas=Image.new("RGBA", (512,512), (0,0,0,0))
            canvas.alpha_composite(image, ((512-image.width)//2, (512-image.height)//2))
        canvas.save(out)
    return out


def prepare_vehicles():
    if not VEHICLES.is_dir():
        raise SystemExit(
            "assets/generated/3ds/vehicles is missing. Run "
            "tools/build_vehicle_assets.py assets/source/vehicles_viv "
            "assets/generated/3ds/vehicles"
        )

    runtime_root = DEST / "vehicles"
    # A PSP component can have a different option inventory from its Zeebo
    # counterpart. Recreate this generated subtree so removed legacy meshes
    # cannot remain addressable beside the migrated configuration.
    if runtime_root.exists():
        shutil.rmtree(runtime_root)
    runtime_root.mkdir(parents=True, exist_ok=True)

    vehicles = []
    traffic_vehicles = []
    texture_count = 0
    part_count = 0

    for src in sorted(p for p in VEHICLES.iterdir() if p.is_dir()):
        cfg = src / "vehicle.cfg"
        if not cfg.exists():
            continue

        dst = runtime_root / src.name
        copy(cfg, dst / "vehicle.cfg")

        for n3p in sorted((src / "parts").rglob("*.n3p")):
            relative = n3p.relative_to(src)
            copy(n3p, dst / relative)
            part_count += 1

        for png in sorted((src / "textures").glob("*.png")):
            prefix = png.name.split("_", 1)[0]
            if not prefix.isdigit():
                continue
            material = int(prefix)
            converter = convert_psp_png if VEHICLES.name == "vehicles_psp" and src.name not in NPC_ONLY_VEHICLES else convert_png
            converter(png, dst / "tex" / f"{material:04d}.t3x")
            texture_count += 1

        (traffic_vehicles if src.name in NPC_ONLY_VEHICLES else vehicles).append(src.name)

    (DEST / "vehicles.txt").write_text(
        "\n".join(vehicles) + "\n", encoding="utf-8"
    )
    # Separate registry means traffic can stream by archetype without touching
    # the player-owned catalogue or attempting customization texture loads.
    (DEST / "traffic_vehicles.txt").write_text(
        "\n".join(traffic_vehicles) + "\n", encoding="utf-8"
    )
    (DEST / "traffic_stream.cfg").write_text(
        "TRAFFIC_STREAM 1\npool 6\nnear_radius 85\nfar_radius 145\nspawn_ahead 24\n",
        encoding="utf-8"
    )
    return len(vehicles), part_count, texture_count


def prepare_vehicle_globals():
    materials_json = GLOBAL / "materials.json"
    wheels_json = GLOBAL / "wheels.json"
    if not materials_json.exists() or not wheels_json.exists():
        raise SystemExit(
            "assets/generated/3ds/vehicle_global is missing. Run "
            "tools/build_vehicle_global_assets.py "
            "assets/source/vehicle_global "
            "assets/source/vehicles_viv/wheels.viv "
            "assets/generated/3ds/vehicle_global"
        )

    global_runtime = DEST / "vehicle_global"
    material_data = json.loads(materials_json.read_text(encoding="utf-8"))
    material_count = 0
    paint_sources = 0

    for row in material_data["materials"]:
        material = int(row["material"])
        source = GLOBAL / row["png"]

        # PAINT is a dynamic renderer class. Preserve its source PNG for
        # inspection, but do not waste 3DS texture memory on the flat mask.
        if material == 1500:
            copy(source, global_runtime / "source" / source.name)
            paint_sources += 1
            continue

        convert_png(source, global_runtime / "tex" / f"{material:04d}.t3x")
        material_count += 1

    copy(materials_json, global_runtime / "materials.json")

    # PSP common.viv has authored tyre, rim and caliper geometry.
    psp_wheels = GENERATED / "wheels_psp" / "runtime"
    tire_mesh = (psp_wheels / "WHEEL_STOCK.n3p" if (psp_wheels / "WHEEL_STOCK.n3p").exists()
                 else GENERATED / "wheels" / "tire_low_poly.n3p")
    if not tire_mesh.exists():
        raise SystemExit("missing generated low-poly tyre mesh")
    copy(tire_mesh, DEST / "wheels" / "tire_low_poly.n3p")

    wheel_data = json.loads(wheels_json.read_text(encoding="utf-8"))
    wheel_runtime = DEST / "wheels"
    wheel_count = 0
    for row in wheel_data["textures"]:
        name = row["name"]
        source = GLOBAL / row["png"]
        convert_png(source, wheel_runtime / "tex" / f"{name}.t3x")
        wheel_count += 1
    # Original PSP atlases use the names queried by VehicleAsset, so they
    # replace Zeebo wheel textures without altering garage selection logic.
    if psp_wheels.is_dir():
        for source in sorted(psp_wheels.glob("*.png")):
            convert_psp_png(source, wheel_runtime / "tex" / (source.stem + ".t3x"))
            wheel_count += 1
    copy(wheels_json, wheel_runtime / "wheels.json")

    return material_count, paint_sources, wheel_count


def prepare_effects():
    effects = GENERATED / "effects"
    for name in ("headlight_bloom.png", "taillight_bloom.png", "tire_smoke.png"):
        source = effects / name
        if not source.exists():
            raise SystemExit(f"missing generated effect sprite: {source}")
        convert_png(source, DEST / "effects" / (source.stem + ".t3x"))


def prepare_audio():
    sounds = {
        "menu": "ui/08_menuselect_rsxb.wav",
        "back": "ui/00_menuback_lsxb.wav",
        "countdown": "ui/30_beeps3_2_1.wav",
        "go": "ui/31_beepsgo.wav",
        "finish": "ui/40_finished.wav",
        "impact": "impact/impact_car_wall_01.wav",
        "nitro": "nitro/turbo_loop.wav",
        "skid": "skid/skid.wav",
        "music": "music/real_tracks/ekstrak_hard_drivers.ogg",
    }
    for key, relative in sounds.items():
        src = NORMALIZED / "audio" / relative
        dst = DEST / "audio" / (key + ".wav")
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not fresh(dst, src):
            subprocess.run(
                [
                    "ffmpeg", "-v", "error", "-y", "-i", str(src),
                    "-map_metadata", "-1", "-ac", "1", "-ar", "22050",
                    "-c:a", "pcm_s16le", str(dst),
                ],
                check=True,
            )
    return len(sounds)


def prepare_quick_loading_wheel():
    """Remove only the white background connected to the icon border.

    White/silver detail inside the wheel remains intact; a simple white-key
    would erase the spokes as well.
    """
    from PIL import Image
    from collections import deque
    src = NORMALIZED / "ui/boot/QuickLoading/01_11_rgba8888.png"
    out = GENERATED / "quickloading_wheel_alpha.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    if not fresh(out, src):
        image = Image.open(src).convert("RGBA")
        pixels = image.load(); width, height = image.size
        queue=deque(); seen=set()
        def pale(x, y):
            r,g,b,a=pixels[x,y]
            return a and r > 224 and g > 224 and b > 224
        for x in range(width): queue.extend(((x,0),(x,height-1)))
        for y in range(height): queue.extend(((0,y),(width-1,y)))
        while queue:
            x,y=queue.popleft()
            if (x,y) in seen or not (0 <= x < width and 0 <= y < height) or not pale(x,y):
                continue
            seen.add((x,y)); r,g,b,a=pixels[x,y]; pixels[x,y]=(r,g,b,0)
            queue.extend(((x+1,y),(x-1,y),(x,y+1),(x,y-1)))
        image.save(out)
    convert_png(out, DEST / "ui/frontend/quick_wheel.t3x")


def prepare_sky_assets():
    """Build a visible horizon backdrop from the recovered Palmont sunset cap.

    The Zeebo CAP is a tiny, dark modulation map intended for its original
    sky geometry.  Drawing it directly as a full-screen texture was nearly
    indistinguishable from the clear colour.  Keep its cloud detail, but bake
    it into a 256x128 RGBA horizon so the 3DS renderer has an actual sky layer.
    """
    from PIL import Image
    src = NORMALIZED / "world/palmont/textured/B1/_decoded/Z0/1236_zeebo_SKY_sunsetA_CAP.png"
    out = GENERATED / "world" / "sky_sunset_runtime.png"
    if not src.exists():
        raise SystemExit(f"missing recovered sky texture: {src}")
    out.parent.mkdir(parents=True, exist_ok=True)
    if not fresh(out, src):
        cap = Image.open(src).convert("RGB").resize((256, 128), Image.Resampling.BICUBIC)
        sky = Image.new("RGBA", (256, 128))
        dst = sky.load(); cloud = cap.load()
        for y in range(128):
            t = y / 127.0
            # Deep blue zenith, warm Palmont sunset at the horizon.
            top, horizon = (12, 23, 40), (174, 76, 36)
            for x in range(256):
                cr, cg, cb = cloud[x, y]
                detail = ((cr + cg + cb) / 3.0 - 32.0) * 0.55
                haze = max(0.0, 1.0 - abs(t - 0.68) / 0.28) * 24.0
                r = max(0, min(255, int(top[0] * (1-t) + horizon[0] * t + detail + haze)))
                g = max(0, min(255, int(top[1] * (1-t) + horizon[1] * t + detail * .62 + haze * .38)))
                b = max(0, min(255, int(top[2] * (1-t) + horizon[2] * t + detail * .30)))
                dst[x, y] = (r, g, b, 255)
        sky.save(out)
    convert_png(out, DEST / "ui/world/sky_sunset.t3x")
    return 1


def prepare_minimap_assets():
    """Rebuild the recovered player marker as a true RGBA texture.

    playerIcon.png was decoded from its original ARGB byte stream as RGBA.
    Its first byte is therefore coverage, while the cyan-looking remaining
    channels are the transparent background.  Reconstructing alpha here keeps
    the marker stable on PICA and avoids an opaque square around the arrow.
    """
    from PIL import Image
    src = ROOT / "assets/source/user_reference/hud_player_icon_raw.png"
    out = GENERATED / "gps_player_rgba.png"
    if not src.exists():
        raise SystemExit(f"missing recovered player marker: {src}")
    if not fresh(out, src):
        raw = Image.open(src).convert("RGBA")
        result = Image.new("RGBA", raw.size)
        pixels = []
        for alpha, red, green, blue in raw.getdata():
            # The extractor stored an ARGB byte stream in RGBA byte order.
            # Reorder it to true RGBA: A,R,G,B -> R,G,B,A.  In particular the
            # cyan-looking canvas has alpha zero after this conversion.
            pixels.append((red, green, blue, alpha))
        result.putdata(pixels)
        result.save(out)
    convert_png(out, DEST / "ui/hud/gps_player.t3x")
    return 1


def prepare_garage_ui():
    """Stage recovered 64x64 customization category icons for native HUD."""
    icons = {
        "bodykits": "generic/bodykits/00_1_rgba8888.png",
        "hoods": "generic/hoods/00_1_rgba8888.png",
        "spoilers": "generic/spoilers/00_1_rgba8888.png",
        "paint": "generic/paint/00_1_rgba8888.png",
        "rims": "generic/rims/00_1_rgba8888.png",
        "engine": "generic/engine/00_1_rgba8888.png",
        "turbo": "generic/turbo/00_1_rgba8888.png",
        "chassis": "generic/chassis/00_1_rgba8888.png",
        "handling": "generic/handling/00_1_rgba8888.png",
        "nitrous": "hud/nitrous/00_1_rgba8888.png",
    }
    for name, relative in icons.items():
        convert_png(NORMALIZED / "ui" / relative,
                    DEST / "ui" / "garage" / f"{name}.t3x")
    frontend = {
        "background": "_ea_graphics_decoded/bootBackground/00_1_rgba8888.png",
        "carbon_logo": "boot/BootPSA/01_3_rgba8888.png",
        "quick_background": "boot/QuickLoading/00_1_rgba8888.png",
        "car_slot": "generic/car5/01_3_rgba8888.png",
    }
    for name, relative in frontend.items():
        convert_png(NORMALIZED / "ui" / relative,
                    DEST / "ui" / "frontend" / f"{name}.t3x")
    generated_count=0
    for name in ("menu_grid", "menu_selection_tight"):
        asset=prepare_main_menu_asset(name)
        if asset:
            convert_png(asset, DEST / "ui" / "frontend" / f"{name}_ai.t3x")
            generated_count += 1
    # Clean, individually cropped layers recovered from tachometer2.
    for name in ("tach_face", "tach_redline", "tach_needle", "tach_gear_digits",
                 "tach_speed_digits", "tach_nitro", "tach_nitro_icon"):
        convert_png(NORMALIZED / "ui/hud_original/cuts" / f"{name}.png",
                    DEST / "ui" / "hud" / f"{name}.t3x")
    # The minimap frame remains independent from the speedometer.
    for name in ("hud_minimap_bezel",):
        asset=prepare_main_menu_asset(name)
        if asset:
            convert_png(asset, DEST / "ui" / "hud" / f"{name}_ai.t3x")
            generated_count += 1
    return len(icons) + len(frontend) + generated_count


def main():
    from build_scene_assets import build
    build()
    subprocess.run(["python3", str(ROOT / "tools/build_road_network.py")], check=True)
    subprocess.run(["python3", str(ROOT / "tools/test_scene.py"), "--validate-only"], check=True)
    for src in sorted((GENERATED / "world").rglob("*")):
        if src.is_file():
            copy(src, DEST / "world" / src.relative_to(GENERATED / "world"))
    vehicles, parts, local_textures = prepare_vehicles()
    global_textures, paint_sources, wheels = prepare_vehicle_globals()
    prepare_quick_loading_wheel()
    garage_icons = prepare_garage_ui()
    minimap_assets = prepare_minimap_assets()
    sky_assets = prepare_sky_assets()
    prepare_effects()
    sounds = prepare_audio()
    print(
        f"Assets: {vehicles} vehicles; {parts} N3P components; "
        f"{local_textures} car-local textures; {global_textures} global "
        f"material textures (+{paint_sources} dynamic PAINT source); "
        f"{wheels} wheel textures; {garage_icons + minimap_assets + sky_assets} frontend/UI textures; "
        f"{sounds} PCM16 sounds"
    )


if __name__ == "__main__":
    main()

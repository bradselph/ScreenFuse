import argparse
import io
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
PNG_FROM = 64
SUPERSAMPLE = 8

PLATE_TOP = (43, 50, 64)
PLATE_BOTTOM = (13, 17, 23)
PLATE_EDGE = (48, 54, 61)
PICTURE_TOP = (88, 166, 255)
PICTURE_BOTTOM = (31, 111, 235)
PICTURE_EDGE = (12, 45, 107)
HILLS_FAR = (56, 139, 253)
HILLS_NEAR = (13, 65, 157)
SUN = (202, 232, 255)
OVERLAY = (63, 185, 80)
OVERLAY_GLOW = (63, 185, 80, 70)
FUSE = (126, 231, 135)


def px(value, size):
    return int(round(value * size))


def box(rect, size):
    return [px(rect[0], size), px(rect[1], size), px(rect[2], size), px(rect[3], size)]


def scaled(rect):
    return [v * SUPERSAMPLE for v in rect]


def gradient(width, height, top, bottom):
    image = Image.new("RGBA", (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(1, height - 1)
        colour = tuple(int(round(top[i] + (bottom[i] - top[i]) * t)) for i in range(3)) + (255,)
        draw.line([(0, y), (width, y)], fill=colour)
    return image


def rounded_mask(canvas_size, rect, radius):
    mask = Image.new("L", canvas_size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(rect, radius=radius, fill=255)
    return mask


def paint_gradient(canvas, rect, radius, top, bottom):
    fill = gradient(canvas.size[0], rect[3] - rect[1], top, bottom)
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    layer.paste(fill, (0, rect[1]))
    mask = rounded_mask(canvas.size, rect, radius)
    canvas.paste(layer, (0, 0), mask)
    return mask


def render(size):
    big = size * SUPERSAMPLE
    canvas = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)

    small = size <= 24
    detailed = size >= 48

    inset = 0.0 if small else 0.035
    plate = scaled(box((inset, inset, 1.0 - inset, 1.0 - inset), size))
    plate[2] -= 1
    plate[3] -= 1
    plate_radius = max(2, px(0.2, size)) * SUPERSAMPLE
    paint_gradient(canvas, plate, plate_radius, PLATE_TOP, PLATE_BOTTOM)
    if not small:
        draw.rounded_rectangle(plate, radius=plate_radius, outline=PLATE_EDGE,
                               width=max(1, px(0.01, size)) * SUPERSAMPLE)

    picture_rect = (0.13, 0.17, 0.67, 0.6) if not small else (0.06, 0.12, 0.68, 0.6)
    picture = scaled(box(picture_rect, size))
    picture_radius = max(1, px(0.055, size)) * SUPERSAMPLE
    picture_mask = paint_gradient(canvas, picture, picture_radius, PICTURE_TOP, PICTURE_BOTTOM)

    if detailed:
        hills = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        hill_draw = ImageDraw.Draw(hills)
        x0, y0, x1, y1 = picture
        w = x1 - x0
        h = y1 - y0
        hill_draw.ellipse([x0 + w * 0.12, y0 + h * 0.16, x0 + w * 0.3, y0 + h * 0.43], fill=SUN)
        hill_draw.polygon([
            (x0, y1), (x0, y0 + h * 0.72), (x0 + w * 0.3, y0 + h * 0.46),
            (x0 + w * 0.52, y0 + h * 0.66), (x0 + w * 0.74, y0 + h * 0.4),
            (x1, y0 + h * 0.62), (x1, y1)], fill=HILLS_FAR)
        hill_draw.polygon([
            (x0, y1), (x0, y0 + h * 0.86), (x0 + w * 0.22, y0 + h * 0.7),
            (x0 + w * 0.5, y0 + h * 0.84), (x0 + w * 0.8, y0 + h * 0.64),
            (x1, y0 + h * 0.78), (x1, y1)], fill=HILLS_NEAR)
        canvas.paste(hills, (0, 0), Image.composite(hills, Image.new("RGBA", canvas.size), picture_mask).getchannel("A"))

    if not small:
        draw.rounded_rectangle(picture, radius=picture_radius, outline=PICTURE_EDGE,
                               width=max(1, px(0.008, size)) * SUPERSAMPLE)

    overlay_rect = (0.33, 0.4, 0.88, 0.84) if not small else (0.32, 0.38, 0.94, 0.88)
    overlay = scaled(box(overlay_rect, size))
    overlay_radius = max(1, px(0.055, size)) * SUPERSAMPLE
    stroke = max(1, px(0.045, size)) * SUPERSAMPLE

    if detailed:
        glow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        spread = max(1, px(0.012, size)) * SUPERSAMPLE
        ImageDraw.Draw(glow).rounded_rectangle(
            [overlay[0] - spread, overlay[1] - spread, overlay[2] + spread, overlay[3] + spread],
            radius=overlay_radius + spread, outline=OVERLAY_GLOW, width=stroke + spread * 2)
        canvas.alpha_composite(glow)

    draw.rounded_rectangle(overlay, radius=overlay_radius, outline=OVERLAY, width=stroke)

    if not small:
        x0, y0, x1, y1 = overlay
        w = x1 - x0
        h = y1 - y0
        bar = max(1, px(0.05, size)) * SUPERSAMPLE
        left = x0 + w * 0.14
        first = y0 + h * 0.5
        draw.rounded_rectangle([left, first, left + w * 0.5, first + bar], radius=bar // 2, fill=OVERLAY)
        if detailed:
            second = first + bar * 1.9
            draw.rounded_rectangle([left, second, left + w * 0.32, second + bar], radius=bar // 2, fill=FUSE)

    if size >= 64:
        cx = overlay[0]
        cy = overlay[1]
        reach = max(2, px(0.075, size)) * SUPERSAMPLE
        waist = max(1, px(0.018, size)) * SUPERSAMPLE
        draw.polygon([(cx, cy - reach), (cx + waist, cy - waist), (cx + reach, cy),
                      (cx + waist, cy + waist), (cx, cy + reach), (cx - waist, cy + waist),
                      (cx - reach, cy), (cx - waist, cy - waist)], fill=(255, 255, 255))

    return canvas.resize((size, size), Image.Resampling.BOX)


def dib_entry(image):
    width, height = image.size
    pixels = image.convert("RGBA").tobytes()
    rows = []
    for y in range(height - 1, -1, -1):
        row = pixels[y * width * 4:(y + 1) * width * 4]
        bgra = bytearray(len(row))
        bgra[0::4] = row[2::4]
        bgra[1::4] = row[1::4]
        bgra[2::4] = row[0::4]
        bgra[3::4] = row[3::4]
        rows.append(bytes(bgra))

    mask_stride = ((width + 31) // 32) * 4
    mask_rows = []
    for y in range(height - 1, -1, -1):
        bits = bytearray(mask_stride)
        for x in range(width):
            if pixels[(y * width + x) * 4 + 3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask_rows.append(bytes(bits))

    xor = b"".join(rows)
    mask = b"".join(mask_rows)
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
    return header + xor + mask


def png_entry(image):
    out = io.BytesIO()
    image.save(out, format="PNG", optimize=True)
    return out.getvalue()


def write_ico(path, images):
    blobs = [png_entry(image) if image.size[0] >= PNG_FROM else dib_entry(image) for image in images]
    offset = 6 + 16 * len(images)
    directory = b""
    for image, blob in zip(images, blobs):
        width, height = image.size
        directory += struct.pack("<BBBBHHII", width % 256, height % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    path.write_bytes(struct.pack("<HHH", 0, 1, len(images)) + directory + b"".join(blobs))


def write_preview(path, images):
    gap = 16
    width = sum(image.size[0] for image in images) + gap * (len(images) + 1)
    height = images[-1].size[1] + gap * 2
    sheet = Image.new("RGBA", (width * 2, height), (0, 0, 0, 0))
    for half, backdrop in enumerate([(240, 240, 240, 255), (32, 32, 32, 255)]):
        panel = Image.new("RGBA", (width, height), backdrop)
        x = gap
        for image in images:
            panel.alpha_composite(image, (x, height - gap - image.size[1]))
            x += image.size[0] + gap
        sheet.paste(panel, (half * width, 0))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description="Draw the ScreenFuse icon at every size and write it as an .ico")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "res" / "screenfuse.ico")
    parser.add_argument("--preview", type=Path, help="also write a PNG sheet of every size on light and dark")
    args = parser.parse_args()

    images = [render(size) for size in SIZES]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_ico(args.out, images)
    print(f"wrote {args.out} ({args.out.stat().st_size} bytes, sizes {', '.join(str(s) for s in SIZES)})")

    if args.preview:
        write_preview(args.preview, images)
        print(f"wrote {args.preview}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

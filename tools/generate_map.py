#!/usr/bin/env python3
"""
Generates resources/images/world_map.png — a 180x90, 3-color (bg/land/coastline)
world map bitmap for the Satellite Tracker watchface, from Natural Earth's public
domain 110m Land dataset. Pure stdlib json + PIL, no shapely/geopandas/numpy.

Pipeline: rasterize at 360x180 (2x supersample, 1px=1deg) via even-odd scanline
point-in-polygon fill -> 3x3 majority filter -> edge-detect coastline -> downscale
with LANCZOS smoothing -> snap back to the exact 3-color palette.

The input GeoJSON isn't committed (see .gitignore) — fetch it first with:
    curl -sL -o tools/data/ne_110m_land.geojson \\
      https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_land.geojson

Usage: python3 tools/generate_map.py
"""

import json
import os

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
GEOJSON_PATH = os.path.join(HERE, "data", "ne_110m_land.geojson")
OUTPUT_PATH = os.path.join(HERE, "..", "resources", "images", "world_map.png")

SUPER_W, SUPER_H = 360, 180
FINAL_W, FINAL_H = 180, 90

COLOR_BG = (0x04, 0x07, 0x0d)
COLOR_LAND = (0x12, 0x3f, 0x30)
COLOR_BRIGHT = (0x2e, 0xe6, 0xa6)


def load_rings(path):
    """Flatten every polygon (exterior + holes) across all features into one
    list of [lon, lat] rings. Even-odd fill handles holes and multiple
    disjoint landmasses correctly without any per-feature bookkeeping."""
    with open(path) as f:
        data = json.load(f)

    rings = []
    for feature in data["features"]:
        geom = feature["geometry"]
        if geom["type"] == "Polygon":
            polygons = [geom["coordinates"]]
        elif geom["type"] == "MultiPolygon":
            polygons = geom["coordinates"]
        else:
            continue
        for polygon in polygons:
            for ring in polygon:
                rings.append(ring)
    return rings


def rasterize(rings, width, height):
    """Even-odd scanline fill. 1 supersample px = 1 degree: px_x = lon+180,
    px_y = 90-lat, so no scaling multiply is needed, just an offset."""
    img = Image.new("RGB", (width, height), COLOR_BG)
    px = img.load()

    # Precompute edges: (lon0, lat0, lon1, lat1) for every ring segment.
    edges = []
    for ring in rings:
        for i in range(len(ring) - 1):
            lon0, lat0 = ring[i]
            lon1, lat1 = ring[i + 1]
            if lat0 != lat1:  # skip horizontal edges, they never cross a scanline
                edges.append((lon0, lat0, lon1, lat1))

    for y in range(height):
        lat = 90.0 - (y + 0.5) * (180.0 / height)
        crossings = []
        for lon0, lat0, lon1, lat1 in edges:
            if (lat0 <= lat < lat1) or (lat1 <= lat < lat0):
                t = (lat - lat0) / (lat1 - lat0)
                lon = lon0 + t * (lon1 - lon0)
                crossings.append(lon)
        crossings.sort()
        for i in range(0, len(crossings) - 1, 2):
            x0 = max(0, min(width - 1, round((crossings[i] + 180.0) * (width / 360.0))))
            x1 = max(0, min(width - 1, round((crossings[i + 1] + 180.0) * (width / 360.0))))
            for x in range(x0, x1 + 1):
                px[x, y] = COLOR_LAND

    return img


def edge_detect(img, width, height):
    """Mark any pixel bordering a different color (4-neighbor) as coastline."""
    src = img.load()
    out = Image.new("RGB", (width, height))
    dst = out.load()
    for y in range(height):
        for x in range(width):
            c = src[x, y]
            is_edge = False
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < width and 0 <= ny < height and src[nx, ny] != c:
                    is_edge = True
                    break
            dst[x, y] = COLOR_BRIGHT if is_edge else c
    return out


def snap_palette(img):
    """LANCZOS resize blends the 3 discrete colors at edges; snap every pixel
    back to the nearest of the exact 3 colors so the shipped PNG stays 3-color."""
    palette = [COLOR_BG, COLOR_LAND, COLOR_BRIGHT]
    src = img.load()
    w, h = img.size
    out = Image.new("RGB", (w, h))
    dst = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b = src[x, y]
            best = min(palette, key=lambda c: (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2)
            dst[x, y] = best
    return out


def main():
    print(f"Loading {GEOJSON_PATH} ...")
    rings = load_rings(GEOJSON_PATH)
    print(f"  {len(rings)} rings")

    print(f"Rasterizing at {SUPER_W}x{SUPER_H} ...")
    img = rasterize(rings, SUPER_W, SUPER_H)

    print("Applying 3x3 majority filter ...")
    img = img.filter(ImageFilter.ModeFilter(size=3))

    print("Edge-detecting coastline ...")
    img = edge_detect(img, SUPER_W, SUPER_H)

    print(f"Downscaling to {FINAL_W}x{FINAL_H} with LANCZOS ...")
    img = img.resize((FINAL_W, FINAL_H), Image.LANCZOS)

    print("Snapping back to 3-color palette ...")
    img = snap_palette(img)

    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    img.save(OUTPUT_PATH)
    print(f"Saved {OUTPUT_PATH}")


if __name__ == "__main__":
    main()

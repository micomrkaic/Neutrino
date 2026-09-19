from matplotlib.textpath import TextPath
from matplotlib.font_manager import FontProperties
import numpy as np

def text_to_svg_path(s, font, size=100, x0=0.0, y0=0.0):
    fp = FontProperties(family=font)
    tp = TextPath((0, 0), s, size=size, prop=fp)
    verts, codes = tp.vertices, tp.codes
    # matplotlib y is up; flip to SVG (y down). Baseline at y0.
    out = []
    i = 0
    while i < len(codes):
        c = codes[i]
        if c == 1:   # MOVETO
            x, y = verts[i]; out.append(f"M{x+x0:.2f} {y0-y:.2f}"); i += 1
        elif c == 2: # LINETO
            x, y = verts[i]; out.append(f"L{x+x0:.2f} {y0-y:.2f}"); i += 1
        elif c == 3: # CURVE3 (quadratic): ctrl, end
            cx, cy = verts[i]; ex, ey = verts[i+1]
            out.append(f"Q{cx+x0:.2f} {y0-cy:.2f} {ex+x0:.2f} {y0-ey:.2f}"); i += 2
        elif c == 4: # CURVE4 (cubic): c1, c2, end
            a = verts[i]; b = verts[i+1]; e = verts[i+2]
            out.append(f"C{a[0]+x0:.2f} {y0-a[1]:.2f} {b[0]+x0:.2f} {y0-b[1]:.2f} {e[0]+x0:.2f} {y0-e[1]:.2f}"); i += 3
        elif c == 79: # CLOSEPOLY
            out.append("Z"); i += 1
        else:
            i += 1
    xs = verts[:,0]; ys = verts[:,1]
    return " ".join(out), (xs.min(), xs.max(), ys.min(), ys.max())

if __name__ == "__main__":
    for font in ["Latin Modern Sans", "DejaVu Sans"]:
        d, bb = text_to_svg_path("Neutrino", font, size=100)
        print(font, "| width=%.1f height=%.1f" % (bb[1]-bb[0], bb[3]-bb[2]), "| chars", len(d))

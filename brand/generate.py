import cairosvg
from textpath import text_to_svg_path

P = dict(navy="#0B1F4D", cyan="#00C2D6", violet="#6A5BFF", blue="#208BFF",
         slate="#5B667A", white="#FFFFFF", ltgray="#C4CCD8")

def icon(c, tx=0, ty=0, s=1.0):
    return f'''<g transform="translate({tx},{ty}) scale({s})">
    <g fill="none" stroke="{c['frame']}" stroke-width="11" stroke-linecap="round" stroke-linejoin="round">
      <path d="M18 44 L18 18 L44 18"/><path d="M84 18 L110 18 L110 44"/>
      <path d="M110 84 L110 110 L84 110"/><path d="M44 110 L18 110 L18 84"/></g>
    <g fill="{c['cell']}">
      <rect x="43" y="43" width="10" height="10" rx="2.5"/><rect x="59" y="43" width="10" height="10" rx="2.5"/><rect x="75" y="43" width="10" height="10" rx="2.5"/>
      <rect x="43" y="59" width="10" height="10" rx="2.5"/><rect x="75" y="59" width="10" height="10" rx="2.5"/>
      <rect x="43" y="75" width="10" height="10" rx="2.5"/><rect x="59" y="75" width="10" height="10" rx="2.5"/><rect x="75" y="75" width="10" height="10" rx="2.5"/></g>
    <g fill="none" stroke-width="3.6" stroke-linecap="round">
      <ellipse cx="64" cy="64" rx="46" ry="18" transform="rotate(-24 64 64)" stroke="{c['orbit1']}"/>
      <ellipse cx="64" cy="64" rx="40" ry="15" transform="rotate(34 64 64)" stroke="{c['orbit2']}" opacity="0.9"/></g>
    <circle cx="64" cy="64" r="7.5" fill="{c['center']}"/>
    <circle cx="22" cy="70" r="6" fill="{c['nvio']}"/>
    <circle cx="96" cy="44" r="5.5" fill="{c['ncya']}"/>
    <circle cx="70" cy="104" r="5" fill="{c['nblu']}"/></g>'''

FULL = dict(frame=P['navy'], cell=P['navy'], center=P['cyan'], orbit1=P['cyan'], orbit2=P['violet'], nvio=P['violet'], ncya=P['cyan'], nblu=P['blue'])
REV  = dict(frame=P['white'], cell=P['white'], center=P['cyan'], orbit1=P['cyan'], orbit2=P['violet'], nvio=P['violet'], ncya=P['cyan'], nblu=P['blue'])
def mono(col): return dict(frame=col, cell=col, center=col, orbit1=col, orbit2=col, nvio=col, ncya=col, nblu=col)

FONT = "Latin Modern Sans"
# wordmark paths (baseline at y=0, glyphs extend upward as negative y)
d_full, bb = text_to_svg_path("Neutrino", FONT, size=100)
d_N,   _   = text_to_svg_path("N", FONT, size=100)
WM_W = bb[1]-bb[0]; WM_TOP = bb[3]   # cap height
d_tag, tbb = text_to_svg_path("A small functional array language", "DejaVu Sans Mono", size=26)
TAG_W = tbb[1]-tbb[0]

def wordmark(fill_main, grad_id):
    # navy word + gradient N overlaid, faux-bolded with a matching stroke
    return (f'<path d="{d_full}" fill="{fill_main}" stroke="{fill_main}" stroke-width="1.6"/>'
            f'<path d="{d_N}" fill="url(#{grad_id})" stroke="url(#{grad_id})" stroke-width="1.6"/>')

GRAD = ('<linearGradient id="{id}" x1="0" y1="0" x2="1" y2="1">'
        '<stop offset="0.4" stop-color="{a}"/><stop offset="0.8" stop-color="{b}"/>'
        '<stop offset="1" stop-color="{c}"/></linearGradient>')

def save(name, svg, w, h, bg=None):
    open(name,'w').write(svg)
    cairosvg.svg2png(url=name, write_to=name.replace('.svg','.png'),
                     output_width=int(w*2), output_height=int(h*2),
                     background_color=bg or 'white')

# ---- horizontal lockup ----
gx = 150  # wordmark x
base = 92 # wordmark baseline y
grad = GRAD.format(id="ng", a=P['navy'], b=P['blue'], c=P['cyan'])
tag_y = base + 34
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {gx+WM_W+30:.0f} 128" width="{gx+WM_W+30:.0f}" height="128">
<defs>{grad}</defs>
{icon(FULL)}
<g transform="translate({gx},{base})">{wordmark(P['navy'],"ng")}</g>
<g transform="translate({gx+2},{tag_y})"><path d="{d_tag}" fill="{P['slate']}"/></g>
</svg>'''
save("logo-horizontal.svg", svg, gx+WM_W+30, 128)
print("horizontal:", int(gx+WM_W+30), "x128  wordmark_w=%.0f tag_w=%.0f"%(WM_W,TAG_W))

# ===== full asset set =====
import math
# tagline sized to match wordmark width
tag_size = 26 * (WM_W / TAG_W)
d_tag2, tbb2 = text_to_svg_path("A small functional array language", "DejaVu Sans Mono", size=tag_size)
TAG2_W = tbb2[1]-tbb2[0]

def frame_svg(w,h,body,defs="",bg=""):
    d = f"<defs>{defs}</defs>" if defs else ""
    b = f'<rect x="0" y="0" width="{w}" height="{h}" rx="{min(w,h)*0.14:.0f}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}" '
            f'width="{w:.0f}" height="{h:.0f}">{d}{b}{body}</svg>')

gN = GRAD.format(id="ng", a=P['navy'], b=P['blue'], c=P['cyan'])
gW = GRAD.format(id="wg", a=P['white'], b=P['blue'], c=P['cyan'])

# 1) icon only
save("icon.svg", frame_svg(128,128, icon(FULL)), 128,128)

# 2) horizontal lockup
gx, base = 150, 92
body = (icon(FULL) + f'<g transform="translate({gx},{base})">{wordmark(P["navy"],"ng")}</g>'
        + f'<g transform="translate({gx+2},{base+30})"><path d="{d_tag2}" fill="{P["slate"]}"/></g>')
W = gx+WM_W+24
save("logo-horizontal.svg", frame_svg(W,128, body, gN), W,128)

# 3) stacked
sx = (max(WM_W, 128)) 
totalW = WM_W + 40
icx = (totalW-128)/2
body = (icon(FULL, tx=icx, ty=6)
        + f'<g transform="translate({(totalW-WM_W)/2:.1f},{158})">{wordmark(P["navy"],"ng")}</g>'
        + f'<g transform="translate({(totalW-TAG2_W)/2:.1f},{188})"><path d="{d_tag2}" fill="{P["slate"]}"/></g>')
save("logo-stacked.svg", frame_svg(totalW,206, body, gN), totalW,206)

# 4) monochrome (navy, no gradient)
body = (icon(mono(P['navy'])) + f'<g transform="translate({gx},{base})"><path d="{d_full}" fill="{P["navy"]}" stroke="{P["navy"]}" stroke-width="1.6"/></g>'
        + f'<g transform="translate({gx+2},{base+30})"><path d="{d_tag2}" fill="{P["navy"]}"/></g>')
save("logo-mono.svg", frame_svg(W,128, body), W,128)

# 5) reversed (dark bg)
body = (f'<rect x="0" y="0" width="{W:.0f}" height="128" rx="18" fill="{P["navy"]}"/>'
        + icon(REV) + f'<g transform="translate({gx},{base})">{wordmark(P["white"],"wg")}</g>'
        + f'<g transform="translate({gx+2},{base+30})"><path d="{d_tag2}" fill="{P["ltgray"]}"/></g>')
save("logo-reversed.svg", frame_svg(W,128, body, gW), W,128, bg=None)

# 6) README header (icon + wordmark + divider + tagline to the right)
hs = 0.62  # scale wordmark
wm_h = WM_TOP*hs
hx = 92
body = (icon(FULL, tx=0, ty=8, s=0.72)
        + f'<g transform="translate({hx},{72}) scale({hs})">{wordmark(P["navy"],"ng")}</g>'
        + f'<line x1="{hx+WM_W*hs+22:.0f}" y1="34" x2="{hx+WM_W*hs+22:.0f}" y2="74" stroke="{P["slate"]}" stroke-width="2" opacity="0.5"/>'
        + f'<g transform="translate({hx+WM_W*hs+38:.0f},{62}) scale(0.62)"><path d="{d_tag2}" fill="{P["slate"]}"/></g>')
HW = hx+WM_W*hs+38+TAG2_W*0.62+20
save("header.svg", frame_svg(HW,104, body, gN), HW,104)

# 7) favicon (simplified: bracket + corner cells + center dot, legible tiny)
fav = f'''<g fill="none" stroke="{P['navy']}" stroke-width="12" stroke-linecap="round" stroke-linejoin="round">
  <path d="M20 46 L20 20 L46 20"/><path d="M82 20 L108 20 L108 46"/>
  <path d="M108 82 L108 108 L82 108"/><path d="M46 108 L20 108 L20 82"/></g>
  <g fill="{P['navy']}"><rect x="42" y="42" width="13" height="13" rx="3"/><rect x="73" y="42" width="13" height="13" rx="3"/>
  <rect x="42" y="73" width="13" height="13" rx="3"/><rect x="73" y="73" width="13" height="13" rx="3"/></g>
  <circle cx="64" cy="64" r="11" fill="{P['cyan']}"/>'''
save("favicon.svg", frame_svg(128,128, fav), 128,128)

print("all variants generated:", "icon horizontal stacked mono reversed header favicon")
print("tag_size=%.1f tag2_w=%.0f headerW=%.0f"%(tag_size,TAG2_W,HW))

# Neutrino — brand assets

Vector logo set for Neutrino, *a small functional array language*. Every file is
a self-contained SVG (pure paths and shapes — no external fonts or images), so it
renders identically everywhere, GitHub READMEs included.

## Files

| File | Use |
|------|-----|
| `icon.svg` | The mark alone (square, 128×128). App icon, avatar, spacing-tight spots. |
| `logo-horizontal.svg` | Primary lockup: icon + wordmark + tagline. |
| `logo-stacked.svg` | Icon above a centered wordmark + tagline. Square-ish layouts. |
| `logo-mono.svg` | Single-colour (deep navy) horizontal lockup. Faxes, stamps, one-ink print. |
| `logo-reversed.svg` | Horizontal lockup on a navy field, for dark backgrounds. |
| `header.svg` | Compact README/repo header: icon + wordmark │ tagline. |
| `favicon.svg` | Simplified mark (bracket + cells + core dot) that stays legible down to 16px. |
| `preview.png` | Contact sheet of every variant. |

## The mark

A scan/matrix **bracket** (the array) framing a 3×3 grid of cells, with a
**particle** at the centre and two crossing **orbits** carrying coloured nodes —
"array language" meeting the neutrino it's named for.

## Colour

| Role | Name | Hex |
|------|------|-----|
| Primary / ink | Deep Navy | `#0B1F4D` |
| Accent / core | Cyan | `#00C2D6` |
| Accent | Violet | `#6A5BFF` |
| Accent | Orbit Blue | `#208BFF` |
| Muted / tagline | Slate Gray | `#5B667A` |

The wordmark's **N** carries a navy→blue→cyan diagonal gradient — the one
deliberate flourish; keep it subtle.

## Type

The wordmark and tagline are **outlined to paths**, so no fonts need installing.
The wordmark is set in a geometric sans (Latin Modern Sans) and the tagline in a
monospace (DejaVu Sans Mono), echoing the language's terminal/`.nu`-script
character. To restyle with a different typeface, re-run `generate.py` (see below)
with the font names changed.

## Usage in a README

```html
<img src="brand/header.svg" alt="Neutrino" height="72">
```

or the full lockup:

```markdown
![Neutrino — a small functional array language](brand/logo-horizontal.svg)
```

## Regenerating / editing

The set is produced by `generate.py` (+ `textpath.py`), which builds the icon
from primitives and outlines the wordmark/tagline via `matplotlib.textpath`.
Change a colour in the `P` palette, a font name, or the geometry, and re-run to
rebuild every variant consistently. The SVGs are also hand-editable — the icon is
plain shapes; the wordmark is a single `<path>` plus a gradient-filled `N`.

## Clear space & minimums

Give the mark at least one bracket-corner of padding on every side. Don't
recolour the wordmark against busy backgrounds — use `logo-reversed.svg` on dark,
`logo-mono.svg` where colour isn't available. Minimum legible icon: ~16px
(use `favicon.svg`); minimum full lockup: ~120px wide.

## Social / OpenGraph card

`og-card.png` (1200×630) is the link-preview image. On GitHub, set it under
**Settings → General → Social preview → Upload an image**. For a project site,
reference it in the page `<head>`:

```html
<meta property="og:image" content="https://…/brand/og-card.png">
<meta name="twitter:card" content="summary_large_image">
```

`og-card.svg` is the editable source (re-render via `generate.py`).

## REPL splash

The interactive REPL greets you with an ASCII rendering of the mark beside a
figlet wordmark, in the brand colours (blue brackets, cyan core, violet orbit
nodes). It appears only on an interactive terminal — piped input and `.nu`
scripts get no banner — and colour is suppressed when stdout isn't a TTY or when
`NO_COLOR` is set. The art lives in `repl.c` (`print_banner`).

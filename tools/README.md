# Widget source tooling — developer only

The UMG widgets in `Content/` are generated from JSON in `widgets/`. The JSON is the
source of truth; the `.uasset` files are build output that happens to be committed
because Unreal cannot load anything else.

**None of this ships.** It is developer tooling. See `widgetgen/README.md` for why the
helper module's manifest is deliberately not called `.uplugin`.

## Why

`.uasset` files are binary: unreviewable in pull requests, unmergeable, and silently
version-locked. All three assets are UE 5.3 format; opening and saving one in 5.4+
upgrades it and breaks every 5.3 user, with nothing to catch it. Commit `0503b80`
("Recreate widgets for Unreal 5.3") is what that looks like when it happens.

## Everyday edit

```sh
# 1. edit the source
$EDITOR widgets/BugReportForm.json

# 2. check it locally - about a second, no Unreal needed
python3 tools/validate_widget_json.py widgets/BugReportForm.json

# 3. regenerate on the build box (UE 5.3), then verify nothing unintended moved
#    see "Regenerating" below
```

## The pieces

| Tool | Runs | Purpose |
|---|---|---|
| `validate_widget_json.py` | anywhere | Structure, widget classes, containment, slot types, and `meta=(BindWidget)` names. Run this first — it is instant. |
| `ue_export_t3d.py` | UE editor | Exports a `.uasset` to Unreal's own `.T3D` text format. |
| `t3d_to_json.py` | anywhere | Converts `.T3D` to the JSON source format. |
| `ue_build_widgets.py` | **UE 5.3 only** | One editor run: generate, export T3D, render both sides with geometry. The everyday command. |
| `ue_generate_widgets.py` | **UE 5.3 only** | Generate only. Also importable — `ue_build_widgets` calls its `run()`. |
| `ue_render_widgets.py` | **UE 5.3 only** | Render + geometry only, for assets you did not just generate. |
| `compare_renders.py` | anywhere | Pixel + geometry diff of two renders. Needs Pillow for the pixel half. |
| `verify_widgets.py` | anywhere | **Pre-release check**: asset format version, and drift between `widgets/*.json` and the real `.uasset`. |
| `ue_verify_widgets.py` | **UE 5.3 only** | Engine half of the check: blueprints compile, every required `BindWidget` resolves. Commandlet, no Slate needed. |
| `compare_widget_json.py` | anywhere | Semantic diff of two widget trees. The regeneration gate. |
| `json_to_html.py` | anywhere | Browser preview laid out by Slate's own algorithm. Within 1px of UE 5.3 on both shipped widgets — see below. |
| `umg_resolve.py` | anywhere | Fills every class default into a widget tree for the preview; refuses classes and fonts it cannot model. |
| `preview/slate_layout.js` | browser | The layout passes, ported from UE 5.3's Slate (`ComputeDesiredSize` / `OnArrangeChildren`). |
| `measure_preview.py` | anywhere | Compares a preview with a real render: per-widget geometry, and pixels with `--png`. Needs Chrome/Chromium. |
| `ue_dump_umg_schema.py` | UE editor | Regenerates `umg_schema.json`. Only needed when changing engine version. |
| `ue_dump_umg_defaults.py` | UE editor | Regenerates `umg_defaults.json`, the class defaults the preview fills in. Same cadence as the schema. |
| `ue_dump_umg_properties.py` | UE editor | Regenerates `umg_properties.json`, every reflected property of the UMG classes, which the markup compiler checks paths against. |
| `umg_markup.py` | anywhere | Compiles `widgets/*.umg.xml` (+ `*.umg.css`) to the JSON the generator reads; `check` finds stale JSON. See "Writing widgets in markup". |
| `ue_import_fonts.py` | **UE 5.3 only** | Builds `Content/Fonts/` from `widgets/fonts/fonts.json` and the TTFs beside it. |
| `widgetgen/` | UE 5.3 | Editor-only C++ module reaching what Python cannot. |

## Regenerating

Must run under **UE 5.3**. Generating on a later engine produces assets 5.3 cannot load —
the exact bug this tooling exists to prevent. `ue_generate_widgets.py` enforces this.

One-time setup on the build box:

```sh
./tools/widgetgen/install.sh "/path/to/HostProject"
# then build the host project's editor target
```

Then, per regeneration, write `bh_generate_config.json` next to the script:

```json
{"json": "…/BugReportForm.json",
 "seed": "/BetaHubBugReporter/BugReportForm",
 "target": "/Game/BH_Gen/BugReportForm_Gen",
 "exportDir": "…/gen"}
```

and run the editor commandlet:

```
UnrealEditor-Cmd.exe HostProject.uproject -run=pythonscript \
  -script=".../ue_generate_widgets.py" -EnablePlugins=PythonScriptPlugin \
  -unattended -nosplash -nopause -stdout
```

Then export the result and diff it against the source to confirm only what you intended
changed:

```sh
python3 tools/t3d_to_json.py generated.T3D -o /tmp/check.json
python3 tools/compare_widget_json.py widgets/BugReportForm.json /tmp/check.json
```

## The edit loop

About a second from a JSON edit to an accurate picture, about 15 to the real thing:

```sh
python3 tools/validate_widget_json.py widgets/BugReportForm.json   # ~1s, catches most mistakes
python3 tools/json_to_html.py widgets/BugReportForm.json -o /tmp/preview.html   # ~1s, layout within 1px
# then one editor run on the box: generate + export + render both sides
python3 tools/compare_renders.py build/BugReportForm_original build/BugReportForm_Gen
```

What the loop does **not** cover, so you know when to open the editor yourself:

- The render is **design-time state**, not what a player sees. `NativeConstruct` calls
  `SetReportType(Bug)`, which collapses several widgets, so the PNG shows a combination
  no user ever gets.
- Default visual state only — no hover, pressed, focused or disabled.
- One draw size. No DPI scaling or viewport-size behaviour.
- Layout and appearance only, never behaviour.

## Seeing the widget

```sh
python3 tools/json_to_html.py widgets/BugReportForm.json -o /tmp/preview.html
python3 tools/json_to_html.py widgets/BugReportForm.json --size 1920x1080 -o /tmp/p.html
open /tmp/preview.html
```

Instant and offline. The preview does not imitate Slate with CSS; it runs Slate's
layout algorithm, ported from the UE 5.3 sources (`preview/slate_layout.js`), on a tree
where `umg_resolve.py` has filled in every class default from `umg_defaults.json`. That
matters because the JSON omits defaults down to single struct fields: a ButtonSlot
padding of `{"Left": 2, "Right": 2}` means Top and Bottom are the ButtonSlot default (2),
not 0. Text uses the engine's own Roboto files (`preview_fonts/`), the font size Slate
computes (points at 96 DPI, rounded to a whole ppem for Roboto), and Slate's per-glyph
advance rounding.

Measured against real UE 5.3 renders (geometry: every widget, at every size listed):

| Widget | Sizes | Geometry | Pixels differing by more than 16 / 64 |
|---|---|---|---|
| BugReportForm | 1280x860, 1920x1080, 1280x800 | all 32 within 1px | 0.8-1.7% / 0.4-0.8% |
| BugReportFormPopup | same | all 10 within 1px | 0.07-0.14% / 0.01-0.03% |
| FontProbe (plugin fonts) | same | all 14 within 1px | 0.9-1.8% / 0.5-1.1% |
| LayoutProbe (direction B classes) | same | all 74 within 1px | 1.4-2.6% / 0.5-1.0% |

The remaining pixel difference is glyph rasterisation and the default style textures
(checkbox, text box), which the preview draws with CSS from measured colours.

Text follows Slate's rules, not the browser's. Latin text takes the KerningOnly shaping
path: glyph advances are rounded to whole pixels, and kerning comes from the legacy
`kern` table only - Barlow and JetBrains Mono have none, so Unreal does not kern them
although a browser would. `LetterSpacing` is thousandths of the point size, truncated to
whole pixels. Auto-wrapping text wraps at the width it had on the previous frame, so the
preview, like `ue_render_widgets.py`, runs three layout passes before drawing.

Limits, so you know when to open the editor anyway:

- Widget classes, slot classes and fonts it has no model for are **refused** with an
  error naming them, never drawn approximately. Adding one means porting its Slate layout
  and, for fonts, adding the TTF to `preview_fonts/` and `FONT_FILES`.
- Default visual state only; no hover, focus or runtime changes (`NativeConstruct`).
- `AutoWrapText` wraps at the arranged width in one pass; Slate settles over two frames.

To compare any preview with a render you have:

```sh
python3 tools/measure_preview.py /tmp/preview.html widgets/BugReportForm.json \
    build/BugReportForm_Gen.geometry.json --png build/BugReportForm_Gen.png
```

`--geometry` mode (`json_to_html.py ... --geometry <render>.geometry.json`) still exists:
it places widgets at the rects Unreal measured, so it is exact by construction but needs
an editor run first.

The fidelity tests in `tests/test_preview.py` compare against
`tests/fixtures/*.geometry.json`, real UE 5.3 renders of the committed assets. Refresh
them with `ue_render_widgets.py` at both sizes whenever the widgets change on purpose.

Headless browsers block `file://` for other pages; the preview is self-contained (fonts
inline), and `measure_preview.py` passes the flags it needs.

## Writing widgets in markup

`widgets/<Asset>.umg.xml` is the hand- and agent-writable source; `widgets/<Asset>.json`
is compiled from it and stays what the generator reads. UMG vocabulary throughout, so
what you write is what Unreal builds:

```xml
<Widget asset="Demo" parent="UserWidget" styles="demo.umg.css">
  <Overlay name="Root">
    <Border class="panel" Padding="24 16" slot.HorizontalAlignment="Center">
      <TextBlock name="Title" class="title" key="Demo.Title">REPORT A BUG</TextBlock>
    </Border>
  </Overlay>
</Widget>
```

```css
:root  { --accent: #FF7A59; }
.title { Font.Size: 36; Font.LetterSpacing: 10; ColorAndOpacity: var(--accent); }
```

```sh
python3 tools/umg_markup.py compile widgets/Demo.umg.xml      # writes widgets/Demo.json
python3 tools/umg_markup.py check                             # every .json matches its markup
python3 tools/umg_markup.py decompile widgets/BugReportForm.json   # JSON -> markup, lossless
```

The full syntax is in the module docstring. What matters day to day:

- Attributes are .T3D property names, dotted into structs (`Font.Size`); `slot.` addresses
  the parent's slot. Paths are checked against `umg_properties.json`, so
  `<TextBlock LetterSpacing=...>` fails with "did you mean Font.LetterSpacing?".
- Shorthands follow Unreal, not CSS: margins are `all`, `horizontal vertical` or
  `left top right bottom` (FMargin's constructors). Colours are sRGB `#RRGGBB[AA]`,
  stored linear.
- One field (`slot.Padding.Bottom="10"`) leaves the others absent, which means default.
- Border and BackgroundBlur keep padding and alignment on themselves *and* on their
  child's slot; the compiler fills whichever side you left out and refuses a contradiction.
- Text without `key=` is culture-invariant (`INVTEXT`), as before.

`widgets/BH_ReportScreen.umg.xml` and its siblings are the shipped example: the report
screen and the rows, prompts, glyphs, frames and toast it is built from, all sharing
`widgets/BH_Widgets.umg.css`.

### Runtime styles

A stylesheet does more than build the asset:

- `var(--bh-*)` values are the theme. Every property set from one is re-coloured when the game
  picks another theme (`FBH_WidgetStyler::SetTheme`); the stylesheet's values are the Dark theme.
- A rule with a widget type and a class (`Border.row--focused`) is a state the game switches at
  runtime (`FBH_WidgetStyler::SetClass`). Such classes may not appear in markup: baked into the
  asset they could never be switched off. Rules with a class only are applied at build time.

```sh
python3 tools/umg_markup.py styles    # writes Source/.../Private/Generated/BH_WidgetStyles.inl
```

`check` fails when that file is stale. A new widget asset needs its C++ parent class (binding its
named widgets) before it can be generated; the generator creates it with `"createIfMissing": true`.

## Fonts

`Content/Fonts/*.uasset` are build output, like the widgets. The source is
`widgets/fonts/fonts.json` plus the TTFs and their licences beside it. A widget selects a
face with `Font.FontObject` (the composite font) and `Font.TypefaceFontName`. Rebuild on
UE 5.3 with `ue_import_fonts.py` (config in its docstring), then copy `Content/Fonts/`
back. The preview reads the same manifest, so a new face needs no preview change.

## Before a release

```sh
python3 tools/verify_widgets.py --emit-config /tmp/bh_verify_config.json
# copy that next to the tools on the build box, run the engine half as a
# commandlet, bring back its .T3D exports, then:
python3 tools/verify_widgets.py --exports <dir-with-the-T3D-exports>
```

Four things get checked, and each has been confirmed to actually fail when it should:

| Check | Catches |
|---|---|
| `FileVersionUE5 == 1009` | an asset saved by a newer engine, which UE 5.3 cannot load |
| JSON vs `.uasset` drift | someone editing one without the other |
| Blueprints compile | a broken asset |
| Required `BindWidget` resolves | a rename that compiles and then nulls at runtime |

## Tests

```sh
python3 tools/tests/test_widget_tools.py
```

## Things worth knowing before you touch this

- **Generation needs a seed asset.** `RootWidget` cannot be written from Python, so the
  generator duplicates an existing asset and rebuilds everything beneath its root. The
  seed's root name and class must match the JSON's.
- **There are two widget trees and both resolve by path.** `Asset.Name_C:WidgetTree` is
  compiled output, regenerated on load — writing there is silently discarded and the save
  still reports success. Only `Asset.Name:WidgetTree` is authoritative.
- **Renaming a widget can break the plugin at runtime**, because `meta=(BindWidget)` binds
  by name and a mismatch is a null rather than a build error. The validator checks this.
- **Nothing here decides whether a look is right.** The gate compares everything Unreal's
  exporter emits and the preview shows faithfully what will be built, but "compiles clean
  and looks wrong" is still a human call.
- **`unreal.log` at Display level never reaches commandlet stdout.** Write to a file, or
  use `log_error`.

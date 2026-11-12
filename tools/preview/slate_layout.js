// Slate layout, re-implemented for the browser.
//
// Input: window.BH_PREVIEW = {tree, width, height}, where tree comes from
// tools/umg_resolve.py (every default already filled in). Output: one
// absolutely positioned <div data-name> per widget, nested like the widget
// tree, at the geometry Slate would compute.
//
// The two passes mirror the engine (UE 5.3 sources named per function):
//   desired()  - bottom-up desired size   (SWidget::ComputeDesiredSize)
//   arrange()  - top-down allotted rects  (SWidget::OnArrangeChildren)
// The browser is used only to measure glyph advances.
//
// Exposes window.bhPreviewReady, a promise that resolves once fonts are
// loaded and the widget is drawn.
(function () {
  "use strict";

  const cfg = window.BH_PREVIEW;
  // Matches RenderSettleFrames in tools/widgetgen: the frames Unreal draws
  // before a render is captured.
  const SETTLE_FRAMES = 3;
  const ctx = document.createElement("canvas").getContext("2d");
  const advanceCache = new Map();

  // ---------------------------------------------------------------- text

  // Width of one line as Slate shapes Latin text (the KerningOnly path of
  // FSlateTextShaper): each glyph's FreeType advance rounded to a whole pixel,
  // plus - between glyphs - the letter spacing and the legacy 'kern' table pair
  // value, itself rounded. Faces without a kern table get no kerning at all,
  // even when the browser would kern them through GPOS.
  // Returns {xs, width}: each glyph's x and the line's width. The same numbers
  // lay the text out and position every drawn glyph, so what you see is what
  // was measured.
  function shapeLine(text, font) {
    const key = font.family + "@" + font.ppem;
    let cache = advanceCache.get(key);
    if (!cache) { cache = new Map(); advanceCache.set(key, cache); }
    ctx.font = font.ppem + "px '" + font.family + "'";
    ctx.fontKerning = "normal";
    const measure = (s) => {
      let v = cache.get(s);
      if (v === undefined) { v = ctx.measureText(s).width; cache.set(s, v); }
      return v;
    };
    const glyphs = Array.from(text);
    const xs = [];
    let x = 0;
    for (let i = 0; i < glyphs.length; i++) {
      xs.push(x);
      const adv = measure(glyphs[i]);
      x += Math.round(adv);
      if (i + 1 < glyphs.length) {
        x += font.letterSpacing;
        if (font.kerning) {
          x += Math.round(measure(glyphs[i] + glyphs[i + 1]) - adv - measure(glyphs[i + 1]));
        }
      }
    }
    return {glyphs, xs, width: x};
  }

  const lineWidth = (text, font) => shapeLine(text, font).width;

  function splitLines(text) {
    return String(text).split(/\r\n|\r|\n/);
  }

  // Greedy word wrap at `limit` pixels, as Slate's default line breaking does
  // for Latin text: break after spaces, never inside a word unless it alone is
  // wider than the line.
  function wrapLines(text, font, limit) {
    const out = [];
    for (const para of splitLines(text)) {
      const words = para.match(/\S+\s*|\s+/g) || [""];
      let line = "";
      for (const w of words) {
        const trial = line + w;
        if (line && lineWidth(trial.trimEnd(), font) > limit) {
          out.push(line.trimEnd());
          line = w;
        } else {
          line = trial;
        }
      }
      out.push(line.trimEnd());
    }
    return out;
  }

  // FTextLayout: each line is the font's max height plus the run's shadow
  // offset (FSlateTextRun::Measure), scaled by LineHeightPercentage.
  function lineStep(font, shadow, lineHeightPct) {
    return (font.lineHeight + shadow[1] + font.outline) * lineHeightPct;
  }

  // `wrapAt` (optional) is the width available to the glyphs; lines longer
  // than that wrap, as with AutoWrapText once Slate knows the width.
  function textSize(text, font, shadow, lineHeightPct, wrapAt) {
    const lines = wrapAt === undefined ? splitLines(text) : wrapLines(text, font, wrapAt);
    const w = Math.max(...lines.map((l) => lineWidth(l, font)));
    return [w + shadow[0] + 2 * font.outline, lineStep(font, shadow, lineHeightPct) * lines.length];
  }

  // Auto-wrapping text wraps at the width it was arranged at on the previous
  // frame (FSlateTextBlockLayout / FSlateEditableTextLayout use CachedSize), so
  // the first pass is unwrapped and later passes wrap. layout() runs as many
  // passes as the Unreal render draws frames.
  function wrapWidth(n, margins) {
    if (!n.autoWrap || n.lastWidth === undefined) return undefined;
    return Math.max(n.lastWidth - margins, 0);
  }

  // ------------------------------------------------------------- helpers

  const H = 0, V = 1;
  const collapsed = (n) => n.visibility === "collapsed";
  // Slate margins are [left, top, right, bottom]; CSS boxes run top, right, bottom, left.
  const cssBox = (m) => m[1] + "px " + m[2] + "px " + m[3] + "px " + m[0] + "px";
  const padAlong = (p, axis) => axis === H ? p[0] + p[2] : p[1] + p[3];

  // LayoutUtils.h AlignChild. Note Center adds MarginPre - MarginPost in full.
  function alignChild(allotted, desired, align, pre, post) {
    const total = pre + post;
    if (align === "fill") return [pre, Math.max(allotted - total, 0)];
    const size = Math.max(Math.min(desired, allotted - total), 0);
    switch (align) {
      case "left": case "top": return [pre, size];
      case "center": return [(allotted - size) / 2 + pre - post, size];
      case "right": case "bottom": return [allotted - size - post, size];
    }
    throw new Error("bad alignment " + align);
  }

  // ArrangeSingleChild: one child inside padding, aligned on both axes.
  function arrangeSingle(child, w, h, padding, hAlign, vAlign) {
    const d = child.desired;
    const x = alignChild(w, d[0], hAlign, padding[0], padding[2]);
    const y = alignChild(h, d[1], vAlign, padding[1], padding[3]);
    arrange(child, x[0], y[0], x[1], y[1]);
  }

  function slotOf(child) {
    if (!child.slot) throw new Error(child.name + " has no slot");
    return child.slot;
  }

  // Padding and alignment a single-child widget lays its content out with.
  function contentLayout(n) {
    const child = n.children[0];
    if (n.kind === "border") {
      const s = slotOf(child);
      return {padding: s.padding, hAlign: s.hAlign, vAlign: s.vAlign};
    }
    if (n.kind === "button") {
      // SButton pads by ContentPadding (the ButtonSlot) + the style's NormalPadding.
      const s = slotOf(child);
      const p = s.padding.map((v, i) => v + n.stylePadding[i]);
      return {padding: p, hAlign: s.hAlign, vAlign: s.vAlign};
    }
    if (n.kind === "blur") {
      const s = slotOf(child);
      return {padding: s.padding, hAlign: s.hAlign, vAlign: s.vAlign};
    }
    if (n.kind === "sizebox") {
      const s = slotOf(child);
      return {padding: s.padding, hAlign: s.hAlign, vAlign: s.vAlign};
    }
    throw new Error("no content layout for " + n.kind);
  }

  // ------------------------------------------------------- desired size

  function desired(n) {
    n.children.forEach(desired);
    n.desired = collapsed(n) ? [0, 0] : computeDesired(n);
    return n.desired;
  }

  function computeDesired(n) {
    const kids = n.children.filter((c) => !collapsed(c));
    switch (n.kind) {
      case "canvas": {
        // SConstraintCanvas::ComputeDesiredSize
        let w = 0, h = 0;
        for (const c of kids) {
          const s = c.slot.canvas, o = s.offsets, a = s.anchors;
          const size = s.autoSize ? c.desired : [o[2], o[3]];
          const dockedX = a[0] === a[2] && (a[0] === 0 || a[0] === 1);
          const dockedY = a[1] === a[3] && (a[1] === 0 || a[1] === 1);
          w = Math.max(w, size[0] + (dockedX ? Math.abs(o[0]) : 0));
          h = Math.max(h, size[1] + (dockedY ? Math.abs(o[1]) : 0));
        }
        return [w, h];
      }
      case "vbox": case "hbox": case "scrollbox": {
        // SBoxPanel ComputeDesiredSizeForBox: fill children count too.
        const main = n.kind === "hbox" ? H : V, cross = 1 - main;
        const out = [0, 0];
        for (const c of kids) {
          const p = c.slot.padding;
          out[main] += c.desired[main] + padAlong(p, main);
          out[cross] = Math.max(out[cross], c.desired[cross] + padAlong(p, cross));
        }
        return out;
      }
      case "overlay": {
        let w = 0, h = 0;
        for (const c of kids) {
          const p = c.slot.padding;
          w = Math.max(w, c.desired[0] + p[0] + p[2]);
          h = Math.max(h, c.desired[1] + p[1] + p[3]);
        }
        return [w, h];
      }
      case "ugrid": {
        // SUniformGridPanel::ComputeDesiredSize: every cell is as big as the largest.
        const p = n.slotPadding;
        let cols = 0, rows = 0, cw = 0, ch = 0;
        for (const c of kids) {
          cols = Math.max(cols, c.slot.column + 1);
          rows = Math.max(rows, c.slot.row + 1);
          cw = Math.max(cw, Math.max(c.desired[0] + p[0] + p[2], n.minSlot[0]));
          ch = Math.max(ch, Math.max(c.desired[1] + p[1] + p[3], n.minSlot[1]));
        }
        n.grid = [cols, rows];
        return [cols * cw, rows * ch];
      }
      case "border": case "button": case "blur": {
        if (!kids.length) {
          if (n.kind === "button") return n.emptySize.slice();
          const p = n.contentPadding;
          return [(p[0] + p[2]) * n.desiredSizeScale[0], (p[1] + p[3]) * n.desiredSizeScale[1]];
        }
        const l = contentLayout(n), d = kids[0].desired;
        const scale = n.kind === "border" ? n.desiredSizeScale : [1, 1];
        return [(d[0] + l.padding[0] + l.padding[2]) * scale[0],
                (d[1] + l.padding[1] + l.padding[3]) * scale[1]];
      }
      case "sizebox": {
        // SBox::ComputeDesiredSize
        const s = n.sizes;
        let w = 0, h = 0;
        if (kids.length) {
          const l = contentLayout(n);
          w = kids[0].desired[0] + l.padding[0] + l.padding[2];
          h = kids[0].desired[1] + l.padding[1] + l.padding[3];
        }
        if (s.MinDesiredWidth !== undefined) w = Math.max(w, s.MinDesiredWidth);
        if (s.MaxDesiredWidth !== undefined) w = Math.min(w, s.MaxDesiredWidth);
        if (s.MinDesiredHeight !== undefined) h = Math.max(h, s.MinDesiredHeight);
        if (s.MaxDesiredHeight !== undefined) h = Math.min(h, s.MaxDesiredHeight);
        if (s.WidthOverride !== undefined) w = s.WidthOverride;
        if (s.HeightOverride !== undefined) h = s.HeightOverride;
        return [w, h];
      }
      case "checkbox": {
        // SCheckBox: [auto: check image] [fill, style padding: content]
        const p = n.contentPadding;
        const content = kids.length ? kids[0].desired : [0, 0];
        return [n.imageSize[0] + content[0] + p[0] + p[2],
                Math.max(n.imageSize[1], content[1] + p[1] + p[3])];
      }
      case "text": {
        const t = textSize(n.text, n.font, n.shadow, n.lineHeightPercentage,
                           wrapWidth(n, n.margin[0] + n.margin[2]));
        const m = n.margin;
        return [Math.max(n.minDesiredWidth, t[0]) + m[0] + m[2], t[1] + m[1] + m[3]];
      }
      case "edittext":
        return editSize(n);
      case "textbox": {
        // SBorder with the style padding around the editable text.
        const e = editSize(n);
        return [e[0] + n.padding[0] + n.padding[2], e[1] + n.padding[1] + n.padding[3]];
      }
      case "spacer": case "image": case "expandable":
        return n.size.slice();
    }
    throw new Error("no desired-size rule for " + n.kind);
  }

  // FSlateEditableTextLayout::ComputeDesiredSize: the text, or the hint while
  // empty, inside the margin plus caret room on both sides; never shorter than
  // the font, never narrower than MinimumDesiredWidth.
  function editSize(n) {
    const shown = n.text || n.hint || "";
    const m = n.margin;
    const t = textSize(shown, n.font, n.shadow, n.lineHeightPercentage,
                       wrapWidth(n, m[0] + m[2] + 2 * n.caretWidth));
    const w = t[0] + 2 * n.caretWidth + m[0] + m[2];
    const h = t[1] + m[1] + m[3];
    return [Math.max(w, n.minDesiredWidth, m[0] + m[2]),
            Math.max(h, n.font.lineHeight, m[1] + m[3])];
  }

  // ------------------------------------------------------------ arrange

  function arrange(n, x, y, w, h) {
    n.rect = [x, y, w, h];
    // The width the editable text inside a text box gets is the box minus its padding.
    n.lastWidth = n.kind === "textbox" ? w - n.padding[0] - n.padding[2] : w;
    const kids = n.children.filter((c) => !collapsed(c));
    switch (n.kind) {
      case "canvas":
        // SConstraintCanvas::ArrangeLayeredChildren
        for (const c of kids) {
          const s = c.slot.canvas, o = s.offsets, a = s.anchors;
          const size = s.autoSize ? c.desired : [o[2], o[3]];
          const ax0 = a[0] * w, ay0 = a[1] * h, ax1 = a[2] * w, ay1 = a[3] * h;
          let cx, cw, cy, ch;
          if (a[0] !== a[2]) { cx = ax0 + o[0]; cw = ax1 - cx - o[2]; }
          else { cx = ax0 + o[0] - size[0] * s.alignment[0]; cw = size[0]; }
          if (a[1] !== a[3]) { cy = ay0 + o[1]; ch = ay1 - cy - o[3]; }
          else { cy = ay0 + o[1] - size[1] * s.alignment[1]; ch = size[1]; }
          arrange(c, cx, cy, cw, ch);
        }
        return;
      case "vbox": case "hbox": case "scrollbox":
        arrangeStack(n, kids, w, h);
        return;
      case "overlay":
        for (const c of kids) arrangeSingle(c, w, h, c.slot.padding, c.slot.hAlign, c.slot.vAlign);
        return;
      case "ugrid": {
        // SUniformGridPanel::OnArrangeChildren
        const [cols, rows] = n.grid;
        const cw = w / cols, ch = h / rows, p = n.slotPadding;
        for (const c of kids) {
          const xr = alignChild(cw, c.desired[0], c.slot.hAlign, p[0], p[2]);
          const yr = alignChild(ch, c.desired[1], c.slot.vAlign, p[1], p[3]);
          arrange(c, cw * c.slot.column + xr[0], ch * c.slot.row + yr[0], xr[1], yr[1]);
        }
        return;
      }
      case "border": case "button": case "sizebox": case "blur":
        if (kids.length) {
          const l = contentLayout(n);
          arrangeSingle(kids[0], w, h, l.padding, l.hAlign, l.vAlign);
        }
        return;
      case "checkbox":
        if (kids.length) {
          const p = n.contentPadding;
          const left = n.imageSize[0];
          const ay = alignChild(h, kids[0].desired[1], "center", p[1], p[3]);
          arrange(kids[0], left + p[0], ay[0], Math.max(w - left - p[0] - p[2], 0), ay[1]);
        }
        return;
      default:
        return;
    }
  }

  // LayoutUtils.h ArrangeChildrenInStack, with AllowShrink as SBoxPanel uses it.
  function arrangeStack(n, kids, w, h) {
    const main = n.kind === "hbox" ? H : V, cross = 1 - main;
    const allotted = [w, h];
    let stretchTotal = 0, fixedTotal = 0;
    for (const c of kids) {
      fixedTotal += padAlong(c.slot.padding, main);
      if (c.slot.sizeRule === "fill") stretchTotal += c.slot.sizeValue;
      else fixedTotal += c.desired[main];
    }
    const nonFixed = Math.max(0, allotted[main] - fixedTotal);
    let pos = 0;
    for (const c of kids) {
      const s = c.slot;
      const size = s.sizeRule === "fill"
        ? (stretchTotal > 0 ? nonFixed * s.sizeValue / stretchTotal : 0)
        : c.desired[main];
      const slotMain = size + padAlong(s.padding, main);
      const slotSize = main === V ? [allotted[H], slotMain] : [slotMain, allotted[V]];
      const xr = alignChild(slotSize[0], c.desired[0], s.hAlign, s.padding[0], s.padding[2]);
      const yr = alignChild(slotSize[1], c.desired[1], s.vAlign, s.padding[1], s.padding[3]);
      if (main === V) arrange(c, xr[0], pos + yr[0], xr[1], yr[1]);
      else arrange(c, pos + xr[0], yr[0], xr[1], yr[1]);
      pos += slotMain;
    }
  }

  // --------------------------------------------------------------- paint

  function applyPaint(el, paint) {
    if (!paint) return;
    el.style.background = paint.fill;
    if (paint.radius.some((r) => r > 0)) {
      // Slate CornerRadii are (TopLeft, TopRight, BottomRight, BottomLeft).
      el.style.borderRadius = paint.radius.map((r) => r + "px").join(" ");
    }
    if (paint.outline) {
      el.style.boxShadow = "inset 0 0 0 " + paint.outline.width + "px " + paint.outline.color;
    }
  }

  // Every glyph is placed at the x the layout computed for it, so letter
  // spacing, Slate's whole-pixel advances and its kerning rules show exactly as
  // they were measured instead of being re-shaped by the browser.
  function textElement(text, font, color, justify, padding, wrap, width, step) {
    const t = document.createElement("div");
    t.className = "bh-text";
    t.style.fontFamily = "'" + font.family + "'";
    t.style.fontSize = font.ppem + "px";
    t.style.lineHeight = font.lineHeight + "px";
    t.style.color = color;
    const inner = width - padding[0] - padding[2];
    const lines = wrap ? wrapLines(text, font, inner) : splitLines(text);
    lines.forEach((line, row) => {
      const shaped = shapeLine(line, font);
      const offset = justify === "center" ? (inner - shaped.width) / 2
        : justify === "right" ? inner - shaped.width : 0;
      shaped.glyphs.forEach((g, i) => {
        if (!g.trim()) return;
        const s = document.createElement("span");
        s.className = "bh-glyph";
        s.textContent = g;
        s.style.left = (padding[0] + offset + shaped.xs[i]) + "px";
        s.style.top = (padding[1] + row * step) + "px";
        t.appendChild(s);
      });
    });
    return t;
  }

  // Arranged rects are relative to the parent, so the DOM nests like the tree
  // and render transforms compose the way Slate's do.
  function draw(n, parentEl) {
    if (collapsed(n)) return;
    const el = document.createElement("div");
    el.className = "bh-w";
    el.dataset.name = n.name;
    el.dataset.class = n.class;
    el.title = n.name + " [" + n.class + "]";
    const [x, y, w, h] = n.rect;
    el.style.left = x + "px";
    el.style.top = y + "px";
    el.style.width = w + "px";
    el.style.height = h + "px";
    if (n.visibility === "hidden") el.style.visibility = "hidden";

    const t = n.transform;
    if (t.scale[0] !== 1 || t.scale[1] !== 1 || t.angle || t.shear[0] || t.shear[1]
        || t.translation[0] || t.translation[1]) {
      el.style.transformOrigin = (n.pivot[0] * 100) + "% " + (n.pivot[1] * 100) + "%";
      el.style.transform = "translate(" + t.translation[0] + "px," + t.translation[1] + "px) "
        + "rotate(" + t.angle + "deg) skew(" + t.shear[0] + "deg," + t.shear[1] + "deg) "
        + "scale(" + t.scale[0] + "," + t.scale[1] + ")";
    }

    switch (n.kind) {
      case "border": case "button": case "image":
        applyPaint(el, n.paint);
        break;
      case "text":
        el.appendChild(textElement(n.text, n.font, n.color, n.justify, n.margin, n.autoWrap,
                                   w, lineStep(n.font, n.shadow, n.lineHeightPercentage)));
        break;
      case "textbox": case "edittext": {
        if (n.kind === "textbox") applyPaint(el, n.paint);
        const p = n.padding || [0, 0, 0, 0], m = n.margin, c = n.caretWidth;
        const box = [p[0] + m[0] + c, p[1] + m[1], p[2] + m[2] + c, p[3] + m[3]];
        el.appendChild(textElement(n.text || n.hint, n.font, n.text ? n.color : n.hintColor,
                                   n.justify, box, n.autoWrap, w,
                                   lineStep(n.font, n.shadow, n.lineHeightPercentage)));
        break;
      }
      case "blur":
        el.style.backdropFilter = "blur(" + n.paint.blur + "px)";
        if (n.paint.radius.some((r) => r > 0)) {
          el.style.borderRadius = n.paint.radius.map((r) => r + "px").join(" ");
        }
        break;
      case "checkbox": {
        // The check image sits centred vertically at the left (SCheckBox).
        const s = n.paint, inset = s.inset;
        const box = document.createElement("div");
        box.className = "bh-check";
        box.style.left = inset + "px";
        box.style.top = (Math.max(0, (h - n.imageSize[1]) / 2) + inset) + "px";
        box.style.width = (n.imageSize[0] - 2 * inset) + "px";
        box.style.height = (n.imageSize[1] - 2 * inset) + "px";
        box.style.borderRadius = s.radius + "px";
        box.style.background = s.tint;
        box.style.opacity = n.checked ? 1 : s.uncheckedAlpha;
        if (n.checked) {
          const tick = document.createElement("div");
          tick.className = "bh-tick";
          tick.style.borderColor = "rgba(0,0,0," + (1 - s.checkmarkAlpha) + ")";
          box.appendChild(tick);
        }
        el.appendChild(box);
        break;
      }
    }
    for (const c of n.children) draw(c, el);
    parentEl.appendChild(el);
  }

  // ------------------------------------------------------------- driver

  function layout() {
    const stage = document.getElementById("bh-stage");
    stage.style.width = cfg.width + "px";
    stage.style.height = cfg.height + "px";
    stage.textContent = "";
    const root = cfg.tree.root;
    for (let frame = 0; frame < SETTLE_FRAMES; frame++) {
      desired(root);
      // FWidgetRenderer hands the root widget the whole draw size.
      arrange(root, 0, 0, cfg.width, cfg.height);
    }
    draw(root, stage);
  }

  window.bhPreviewReady = document.fonts.ready
    .then(() => Promise.all(Object.keys(cfg.tree.fonts).map(
      (f) => document.fonts.load("16px '" + f + "'"))))
    .then(layout);
})();

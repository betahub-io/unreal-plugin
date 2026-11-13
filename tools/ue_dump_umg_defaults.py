"""Dump the class-default values of the UMG properties that drive layout.

The widget JSON only holds non-default values, because Unreal's .T3D export
omits anything equal to the class default - including single fields of a
struct. `"Padding": {"Left": 2, "Right": 2}` on a ButtonSlot means Top and
Bottom are the ButtonSlot default (2), not zero. Anything that lays a widget
out offline (tools/json_to_html.py) needs those defaults, and they must come
from the engine rather than from memory.

Runs inside the editor, once per engine version. Needs a booted editor rather
than a commandlet: button, checkbox and text-box defaults are copied from the
UMG core style, which commandlets never initialise.

    UnrealEditor-Cmd.exe <project>.uproject \
        -ExecutePythonScript="tools/ue_dump_umg_defaults.py" \
        -EnablePlugins=PythonScriptPlugin -AllowCommandletRendering \
        -unattended -nosplash -nopause -stdout

Writes bh_umg_defaults.json beside this script; copy it to
tools/umg_defaults.json. Values use the same JSON shape as widgets/*.json,
because both go through t3d_to_json.parse_value.
"""

import json
import os
import re
import sys
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import t3d_to_json  # noqa: E402

OUT = os.path.join(HERE, "bh_umg_defaults.json")

# Class -> properties, in the names .T3D (and so the widget JSON) uses.
PROPERTIES = {
    "Widget": ["Visibility", "RenderTransform", "RenderTransformPivot"],
    "TextBlock": ["Font", "ShadowOffset", "ColorAndOpacity", "Margin",
                  "Justification", "AutoWrapText", "MinDesiredWidth",
                  "LineHeightPercentage"],
    "Border": ["Padding", "HorizontalAlignment", "VerticalAlignment",
               "Background", "BrushColor", "DesiredSizeScale"],
    "BorderSlot": ["Padding", "HorizontalAlignment", "VerticalAlignment"],
    "Button": ["WidgetStyle", "BackgroundColor", "ColorAndOpacity"],
    "ButtonSlot": ["Padding", "HorizontalAlignment", "VerticalAlignment"],
    "CheckBox": ["WidgetStyle", "CheckedState"],
    "VerticalBoxSlot": ["Padding", "Size", "HorizontalAlignment",
                        "VerticalAlignment"],
    "HorizontalBoxSlot": ["Padding", "Size", "HorizontalAlignment",
                          "VerticalAlignment"],
    "OverlaySlot": ["Padding", "HorizontalAlignment", "VerticalAlignment"],
    "CanvasPanelSlot": ["LayoutData", "bAutoSize", "ZOrder"],
    "SizeBox": ["WidthOverride", "HeightOverride", "MinDesiredWidth",
                "MinDesiredHeight", "MaxDesiredWidth", "MaxDesiredHeight"],
    "SizeBoxSlot": ["Padding", "HorizontalAlignment", "VerticalAlignment"],
    "Spacer": ["Size"],
    "Image": ["Brush", "ColorAndOpacity"],
    "MultiLineEditableTextBox": ["WidgetStyle", "Justification",
                                 "AutoWrapText"],
    "EditableTextBox": ["WidgetStyle", "Justification", "MinimumDesiredWidth"],
    "ScrollBox": ["WidgetStyle", "WidgetBarStyle", "Orientation",
                  "ScrollBarVisibility", "ScrollbarThickness",
                  "ScrollbarPadding", "AlwaysShowScrollbar"],
    "ScrollBoxSlot": ["Padding", "Size", "HorizontalAlignment", "VerticalAlignment"],
    "BackgroundBlur": ["Padding", "HorizontalAlignment", "VerticalAlignment",
                       "BlurStrength", "CornerRadius", "bApplyAlphaToBlur"],
    "BackgroundBlurSlot": ["Padding", "HorizontalAlignment", "VerticalAlignment"],
    "UniformGridPanel": ["SlotPadding", "MinDesiredSlotWidth", "MinDesiredSlotHeight"],
    "UniformGridSlot": ["Row", "Column", "HorizontalAlignment", "VerticalAlignment"],
    "EditableText": ["WidgetStyle", "Justification", "MinimumDesiredWidth"],
    "MultiLineEditableText": ["WidgetStyle", "Justification", "AutoWrapText", "Margin",
                              "LineHeightPercentage"],
    "ExpandableArea": ["Style", "BorderBrush", "BorderColor", "AreaPadding",
                       "HeaderPadding", "bIsExpanded"],
}

lines = []


def log(m):
    lines.append(str(m))


def python_name(t3d_name):
    """BrushColor -> brush_color, bAutoSize -> auto_size (UE's Python naming)."""
    name = re.sub(r"^b(?=[A-Z])", "", t3d_name)
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


def to_json(value):
    """Turn a Python-wrapped engine value into the widget JSON shape."""
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value
    if isinstance(value, unreal.EnumBase):
        # Python spells enum members H_ALIGN_FILL; .T3D spells them HAlign_Fill.
        # Keep both so the consumer never has to guess.
        return {"__enum__": value.name, "__value__": int(value.value)}
    if isinstance(value, unreal.StructBase):
        return t3d_to_json.parse_value(value.export_text())
    if isinstance(value, unreal.Text):
        return {"__text__": str(value)}
    if isinstance(value, unreal.Object):
        return {"__ref__": value.get_path_name(), "__class__": value.get_class().get_name()}
    if value is None:
        return None
    return str(value)


out = {"engine": unreal.SystemLibrary.get_engine_version(), "classes": {}}
errors = 0
try:
    for cls_name, props in PROPERTIES.items():
        cls = getattr(unreal, cls_name, None)
        if cls is None:
            log("MISSING CLASS %s" % cls_name)
            errors += 1
            continue
        cdo = unreal.get_default_object(cls)
        values = {}
        for prop in props:
            try:
                values[prop] = to_json(cdo.get_editor_property(python_name(prop)))
            except Exception as exc:  # protected or renamed in this engine
                log("UNREADABLE %s.%s: %s" % (cls_name, prop, exc))
                errors += 1
        out["classes"][cls_name] = values

    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2, sort_keys=True)
    log("wrote %s" % OUT)
except Exception:
    errors += 1
    log("FATAL:\n%s" % traceback.format_exc())

log("BH_DEFAULTS_RESULT: %s (%d problems)" % ("OK" if not errors else "PROBLEMS", errors))
with open(os.path.join(HERE, "bh_umg_defaults_report.txt"), "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines))
unreal.log_error("BH_DEFAULTS_RESULT: %d problems" % errors)

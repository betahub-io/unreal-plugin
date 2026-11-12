"""Build the plugin's font assets from widgets/fonts/fonts.json and the TTFs beside it.

MUST run under UE 5.3, for the same reason as the widget generator: the
assets are committed and every supported engine has to load them.

Per font in the manifest it imports each TTF as a FontFace named
<Family>_<Typeface> beside the font, then creates (or updates) a runtime
composite Font whose default typeface lists those faces by name. Widgets
then select a face with Font.TypefaceFontName, exactly as with Roboto.

Python cannot reach FontData.FontFaceAsset, so the composite font is written
through BHWidgetGenLibrary.ApplyPropertyText (tools/widgetgen), which goes
through Unreal's own text import.

Needs the editor booted (font import touches Slate):

    UnrealEditor-Cmd.exe <project>.uproject \
        -ExecutePythonScript="tools/ue_import_fonts.py" \
        -EnablePlugins=PythonScriptPlugin -AllowCommandletRendering \
        -unattended -nosplash -nopause -stdout

Reads bh_fonts_config.json from its own directory:

    {"manifest": "C:/.../widgets/fonts/fonts.json",
     "fontsDir": "C:/.../widgets/fonts"}

and writes fonts_report.txt beside itself.
"""

import json
import os
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "bh_fonts_config.json")
REQUIRED_ENGINE_PREFIX = "5.3."
FACE_CLASS = "/Script/Engine.FontFace"
RUNTIME_CACHE = "Runtime"

lines = []
problems = []


def log(m):
    lines.append(str(m))


def problem(m):
    problems.append(str(m))
    lines.append("PROBLEM: %s" % m)


def face_name(font_asset, typeface):
    return "%s_%s" % (font_asset.rsplit("/", 1)[-1], typeface)


def import_faces(font_asset, typefaces, fonts_dir):
    folder = font_asset.rsplit("/", 1)[0]
    tasks = []
    for typeface, ttf in typefaces.items():
        path = os.path.join(fonts_dir, ttf)
        if not os.path.isfile(path):
            raise RuntimeError("%s: %s not found" % (font_asset, path))
        task = unreal.AssetImportTask()
        task.filename = path.replace("\\", "/")
        task.destination_path = folder
        task.destination_name = face_name(font_asset, typeface)
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.factory = unreal.FontFileImportFactory()
        tasks.append((typeface, task))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for _, t in tasks])
    faces = {}
    for typeface, task in tasks:
        paths = list(task.imported_object_paths)
        if len(paths) != 1:
            raise RuntimeError("%s %s: import produced %r" % (font_asset, typeface, paths))
        face = unreal.load_asset(paths[0])
        if face is None or face.get_class().get_name() != "FontFace":
            raise RuntimeError("%s %s: %s is not a FontFace" % (font_asset, typeface, paths[0]))
        faces[typeface] = paths[0]
        log("  face %-10s <- %s" % (typeface, typefaces[typeface]))
    return faces


def composite_text(faces):
    entries = ",".join(
        '(Name="%s",Font=(FontFaceAsset="%s\'%s\'"))' % (name, FACE_CLASS, path)
        for name, path in faces.items())
    return "(DefaultTypeface=(Fonts=(%s)))" % entries


def build_font(font_asset, faces):
    lib = unreal.BHWidgetGenLibrary
    font = unreal.load_asset(font_asset)
    if font is None:
        folder, name = font_asset.rsplit("/", 1)
        font = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.Font, unreal.FontFactory())
    if font is None:
        raise RuntimeError("%s: could not create the Font asset" % font_asset)
    if not lib.apply_property_text(font, "FontCacheType", RUNTIME_CACHE):
        raise RuntimeError("%s: could not set FontCacheType" % font_asset)
    if not lib.apply_property_text(font, "CompositeFont", composite_text(faces)):
        raise RuntimeError("%s: could not set CompositeFont" % font_asset)
    if not unreal.EditorAssetLibrary.save_asset(font_asset, only_if_is_dirty=False):
        raise RuntimeError("%s: save failed" % font_asset)
    log("  font %s: %s" % (font_asset, ", ".join(faces)))


try:
    engine = unreal.SystemLibrary.get_engine_version()
    log("engine: %s" % engine)
    with open(CONFIG_PATH, "r", encoding="utf-8") as fh:
        cfg = json.load(fh)
    if not engine.startswith(REQUIRED_ENGINE_PREFIX) and not cfg.get("allowAnyEngine"):
        raise RuntimeError("refusing to import fonts under %s: the assets must be saved "
                           "by UE 5.3 or 5.3 users cannot load them" % engine)
    if getattr(unreal, "BHWidgetGenLibrary", None) is None:
        raise RuntimeError("BHWidgetGenLibrary not loaded - install tools/widgetgen first")
    with open(cfg["manifest"], "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    for entry in manifest["fonts"]:
        log(entry["asset"])
        try:
            faces = import_faces(entry["asset"], entry["typefaces"], cfg["fontsDir"])
            build_font(entry["asset"], faces)
        except Exception:
            problem("%s:\n%s" % (entry["asset"], traceback.format_exc()))
except Exception:
    problem("FATAL:\n%s" % traceback.format_exc())

result = "OK" if not problems else "PROBLEMS"
log("BH_FONTS_RESULT: %s (%d problems)" % (result, len(problems)))
with open(os.path.join(HERE, "fonts_report.txt"), "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines))
unreal.log_error("BH_FONTS_RESULT: %s (%d problems)" % (result, len(problems)))

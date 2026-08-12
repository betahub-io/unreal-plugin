"""Generate a WidgetBlueprint's widget tree from the JSON source format.

MUST run under UE 5.3. Later engines add WidgetVariableNameToGuidMap, which
programmatic widget creation cannot populate from Python, so generating there
trips compiler ensures. 5.3 has no such map.

Runs inside the editor:

    UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript \
        -script="tools/ue_generate_widgets.py" -EnablePlugins=PythonScriptPlugin \
        -unattended -nosplash -nopause -stdout

Reads bh_generate_config.json from its own directory:

    {"json": "C:/.../BugReportForm.json",
     "seed": "/BetaHubBugReporter/BugReportForm",
     "target": "/Game/BH_Gen/BugReportForm_Gen",
     "exportDir": "C:/.../gen_export"}

RootWidget cannot be written from Python, so generation starts from a seed
asset and rebuilds everything below its existing root. The root's own class
and name therefore come from the seed, not the JSON, and the two must agree.

Property failures are collected rather than raised, so a single run reports
every unsupported property instead of stopping at the first.
"""

import json
import os
import re
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "bh_generate_config.json")

lines = []
problems = []


def log(m):
    lines.append(str(m))


def problem(m):
    problems.append(str(m))
    lines.append("PROBLEM: %s" % m)


def mark(m):
    unreal.log_error("BH_GEN: %s" % m)
    log(">>> %s" % m)


# ---------------------------------------------------------------- properties

def to_snake(name):
    s = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    return s.lower()


def prop_get(obj, name):
    last = None
    for n in (name, to_snake(name)):
        try:
            return obj.get_editor_property(n)
        except Exception as exc:
            last = exc
    raise KeyError("%s (%s)" % (name, last))


def prop_set(obj, name, value):
    last = None
    for n in (name, to_snake(name)):
        try:
            obj.set_editor_property(n, value)
            return
        except Exception as exc:
            last = exc
    raise KeyError("%s (%s)" % (name, last))


def norm(token):
    return re.sub(r"[^A-Z0-9]", "", str(token).upper())


def match_enum(enum_cls, token):
    want = norm(token)
    members = []
    try:
        members = list(enum_cls)
    except Exception:
        members = [getattr(enum_cls, n) for n in dir(enum_cls)
                   if not n.startswith("_")]
    for m in members:
        name = getattr(m, "name", None)
        if name is not None and norm(name) == want:
            return m
    # UMG enum literals often carry a prefix the Python name drops
    # (HAlign_Fill vs H_ALIGN_FILL handled above; Hidden vs HIDDEN too).
    for m in members:
        name = getattr(m, "name", None)
        if name is not None and norm(name).endswith(want):
            return m
    raise ValueError("no member of %s matches %r" % (enum_cls, token))


def coerce(current, value, path):
    """Turn a JSON value into something set_editor_property will accept.

    The existing (default) value is used to discover the target type, which
    avoids maintaining a hand-written table of UMG struct types.
    """
    if isinstance(value, dict) and "__text__" in value:
        return unreal.Text(value["__text__"])

    if isinstance(value, dict) and "__ref__" in value:
        ref = value["__ref__"]
        loaded = unreal.load_object(None, ref)
        if loaded is None:
            loaded = unreal.load_asset(ref)
        if loaded is None:
            raise ValueError("could not load object %r for %s" % (ref, path))
        return loaded

    if isinstance(current, unreal.StructBase) and isinstance(value, dict):
        for k, v in value.items():
            try:
                sub_current = prop_get(current, k)
            except KeyError:
                raise ValueError("no field %r on %s at %s"
                                 % (k, type(current).__name__, path))
            prop_set(current, k, coerce(sub_current, v, "%s.%s" % (path, k)))
        return current

    if isinstance(current, unreal.EnumBase) and isinstance(value, str):
        return match_enum(type(current), value)

    if isinstance(current, float) and isinstance(value, int):
        return float(value)

    return value


# Developer-only helper module (tools/widgetgen). Reaches what Python cannot:
# localisable FText and protected properties such as UWidget::bIsVariable.
HELPER = getattr(unreal, "BHWidgetGenLibrary", None)


def t3d_escape(s):
    return (s.replace("\\", "\\\\").replace('"', '\\"')
             .replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t"))


def as_t3d_text(value):
    """Re-serialise a JSON value as .T3D property text, or None if unsupported."""
    if isinstance(value, dict) and "__text__" in value:
        key = value.get("__key__")
        literal = t3d_escape(value["__text__"])
        if key:
            return 'NSLOCTEXT("", "%s", "%s")' % (key, literal)
        return 'INVTEXT("%s")' % literal
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return value
    return None


def apply_via_helper(obj, name, value):
    if HELPER is None:
        return False
    text = as_t3d_text(value)
    if text is None:
        return False
    try:
        return bool(HELPER.apply_property_text(obj, name, text))
    except Exception:
        return False


def apply_props(obj, props, where):
    for name, value in (props or {}).items():
        # An FText carrying a localisation key must go through the helper:
        # unreal.Text() only ever produces culture-invariant text, which would
        # silently drop the key and break localisation.
        if isinstance(value, dict) and value.get("__key__"):
            if apply_via_helper(obj, name, value):
                continue
            problem("%s: helper could not set localisable text %s" % (where, name))

        try:
            current = prop_get(obj, name)
        except KeyError as exc:
            # Protected properties are unreadable from Python but reachable by
            # reflection inside the helper.
            if apply_via_helper(obj, name, value):
                continue
            problem("%s: cannot read %s" % (where, exc))
            continue

        try:
            prop_set(obj, name, coerce(current, value, "%s.%s" % (where, name)))
        except Exception as exc:
            if apply_via_helper(obj, name, value):
                continue
            problem("%s: cannot set %s -> %s" % (where, name, exc))


# ---------------------------------------------------------------- tree build

def descendants(widget):
    out = []
    if isinstance(widget, unreal.PanelWidget):
        for i in range(widget.get_children_count()):
            child = widget.get_child_at(i)
            if child is not None:
                out.append(child)
                out.extend(descendants(child))
    return out


def strip_below(root):
    """Detach everything under root and free the names for regeneration."""
    existing = descendants(root)
    for w in reversed(existing):
        parent = w.get_parent()
        if parent is not None:
            parent.remove_child(w)
    for w in existing:
        try:
            w.rename("%s_BHOLD" % w.get_name())
        except Exception as exc:
            problem("could not rename %s out of the way: %s" % (w.get_name(), exc))
    return [w.get_name() for w in existing]


def build(node, parent, tree, depth=0):
    cls_name = node["class"]
    cls = getattr(unreal, cls_name, None)
    if cls is None:
        problem("unknown widget class %r for %r" % (cls_name, node["name"]))
        return None

    try:
        widget = unreal.new_object(cls, outer=tree, name=node["name"])
    except Exception as exc:
        problem("could not create %s [%s]: %s" % (node["name"], cls_name, exc))
        return None

    apply_props(widget, node.get("properties"), node["name"])

    slot = None
    try:
        slot = parent.add_child(widget)
    except Exception as exc:
        problem("could not attach %s to %s: %s"
                % (node["name"], parent.get_name(), exc))

    slot_spec = node.get("slot")
    if slot is not None and slot_spec:
        want = slot_spec.get("class")
        got = slot.get_class().get_name()
        if want and want != got:
            problem("%s: slot class mismatch, json says %s but parent produced %s"
                    % (node["name"], want, got))
        apply_props(slot, slot_spec.get("properties"), "%s.slot" % node["name"])

    for child in node.get("children", []):
        build(child, widget, tree, depth + 1)
    return widget


# ---------------------------------------------------------------- main


def run(cfg=None):
    """Generate one widget. Returns (ok, problems). Importable so the
    combined build driver can generate and render in a single editor
    boot rather than paying the startup cost twice."""
    global lines, problems
    lines, problems = [], []
    if cfg is None:
        with open(CONFIG_PATH, 'r', encoding='utf-8') as fh:
            cfg = json.load(fh)

    ok = True
    try:

        json_path = cfg["json"]
        seed = cfg["seed"]
        target = cfg["target"]
        export_dir = cfg.get("exportDir")

        with open(json_path, "r", encoding="utf-8") as fh:
            doc = json.load(fh)

        # Fail fast on the wrong engine. Generating on 5.4+ silently produces
        # assets UE 5.3 cannot load, and on 5.7+ also trips compiler ensures.
        engine_version = unreal.SystemLibrary.get_engine_version()
        log("engine       : %s" % engine_version)
        if not engine_version.startswith("5.3.") and not cfg.get("allowAnyEngine"):
            raise RuntimeError(
                "refusing to generate on engine %s - widgets must be generated "
                "under UE 5.3 or the output cannot be loaded by 5.3 users. "
                'Set "allowAnyEngine": true in the config only for experiments '
                "whose output will be thrown away." % engine_version)

        if HELPER is None:
            problem("BHWidgetGenLibrary not loaded - localisation keys and "
                    "protected properties will be lost. Run "
                    "tools/widgetgen/install.sh against this project and rebuild.")
        else:
            log("helper       : BHWidgetGenLibrary available")

        log("source json  : %s" % json_path)
        log("seed asset   : %s" % seed)
        log("target asset : %s" % target)

        if unreal.EditorAssetLibrary.does_asset_exist(target):
            unreal.EditorAssetLibrary.delete_asset(target)
        bp = unreal.EditorAssetLibrary.duplicate_asset(seed, target)
        if bp is None:
            raise RuntimeError("could not duplicate %s -> %s" % (seed, target))

        asset_name = target.rsplit("/", 1)[-1]
        tree = unreal.load_object(None, "%s.%s:WidgetTree" % (target, asset_name))
        if tree is None:
            raise RuntimeError("no authored WidgetTree on %s" % target)

        root_spec = doc["root"]
        root = unreal.load_object(
            None, "%s.%s:WidgetTree.%s" % (target, asset_name, root_spec["name"]))
        if root is None:
            raise RuntimeError(
                "seed has no root named %r - seed and json must agree on the root"
                % root_spec["name"])
        if root.get_class().get_name() != root_spec["class"]:
            raise RuntimeError("root class mismatch: seed=%s json=%s"
                               % (root.get_class().get_name(), root_spec["class"]))

        mark("STRIP-BEGIN")
        removed = strip_below(root)
        log("stripped %d widgets: %s" % (len(removed), removed))
        mark("STRIP-END")

        mark("BUILD-BEGIN")
        apply_props(root, root_spec.get("properties"), root_spec["name"])
        for child in root_spec.get("children", []):
            build(child, root, tree)
        mark("BUILD-END")

        mark("COMPILE-BEGIN")
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        mark("COMPILE-END")

        unreal.EditorAssetLibrary.save_asset(target, only_if_is_dirty=False)
        log("saved %s" % target)

        if export_dir:
            os.makedirs(export_dir, exist_ok=True)
            unreal.AssetToolsHelpers.get_asset_tools().export_assets(
                [target], export_dir)
            log("exported to %s" % export_dir)

    except Exception:
        ok = False
        log("FATAL:\n%s" % traceback.format_exc())

    log("")
    log("problems: %d" % len(problems))
    for p in problems:
        log("  %s" % p)

    result = "OK" if (ok and not problems) else ("PROBLEMS" if ok else "FAILED")
    log("BH_GEN_RESULT: %s" % result)

    report_dir = cfg.get("exportDir") or HERE
    try:
        os.makedirs(report_dir, exist_ok=True)
        with open(os.path.join(report_dir, "generate_report.txt"), "w",
                  encoding="utf-8") as fh:
            fh.write("\n".join(lines))
    except Exception:
        pass

    mark("BH_GEN_RESULT: %s (%d problems)" % (result, len(problems)))

    return ok, problems


if __name__ == "__main__":
    run()

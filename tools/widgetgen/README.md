# BetaHub Widget Generator — developer tool

**This is not part of the shipped plugin and must never reach customers.**

A tiny editor-only Unreal module used when regenerating `Content/BugReportForm.uasset`
from `widgets/*.json`. It exists solely to set the handful of properties Unreal's Python
API cannot reach.

## Why it exists

Python's `set_editor_property` covers almost everything, but two property classes are
out of reach, confirmed by round-trip testing:

| Gap | Why Python can't do it |
|---|---|
| `FText` localisation keys | `unreal.Text()` only produces culture-invariant text, so `NSLOCTEXT("", "KEY", "Submit")` degrades to `INVTEXT("Submit")` and the key is lost |
| `bIsVariable` | declared `UPROPERTY()` in a protected section of `Widget.h`; Unreal's Python layer enforces C++ access |

`ApplyPropertyText` sidesteps both by going through Unreal's **reflection** system
(`FProperty::ImportText_Direct`), which parses the same text format `.T3D` exports use and
does not honour C++ access specifiers. That is the entire point of the module.

## Why the manifest is called `.uplugin.in`

The repository root *is* the shipped plugin — `BetaHubBugReporter.uplugin` sits beside this
directory. A file named `*.uplugin` anywhere in this tree would be shipped to customers in
the release archive, and risks being picked up by Unreal's plugin discovery. Keeping the
template inert in-tree and materialising it only on a developer machine makes it impossible
to ship by accident.

## Use

```sh
./tools/widgetgen/install.sh "/mnt/c/Users/upsoft/Documents/Unreal Projects/BetaHub_5_3"
```

Then build the host project's editor target. See `tools/README.md` for the full
regeneration workflow.

## Engine version

Must be built and run under **UE 5.3**. Generating widgets under any later engine produces
assets UE 5.3 cannot load. The generator script refuses to run on other engines.

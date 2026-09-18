#!/usr/bin/env python3
"""Fail if any Public/ header includes a header that lives only in Private/.

Unreal Build Tool exposes a module's Public/ folder to *consuming* C++ modules
but keeps Private/ on the include path only while the module compiles itself.
So a public header that `#include`s a private one builds fine inside the plugin
and fails with C1083 ("No such file or directory") the moment a customer's own
module includes it. That is invisible in Blueprint and in the plugin's own build
- it only bites external C++ consumers.

This has already shipped once (1.6.x / 1.7.0: Public/BH_ReportFormWidget.h ->
Private/BH_GameRecorder.h). This check guards against a repeat. It runs in well
under a second with no Unreal install.

Usage:
    python3 tools/check_public_headers.py
    python3 tools/check_public_headers.py --module Source/BetaHubBugReporter
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

DEFAULT_MODULE = os.path.join("Source", "BetaHubBugReporter")

# Local include of another BH header, e.g. #include "BH_GameRecorder.h"
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)

# .generated.h is produced by UHT next to the header; not a real file to resolve.
GENERATED_SUFFIX = ".generated.h"


def header_names(directory):
    """Basenames of .h files directly in `directory` (non-recursive is enough;
    UBT resolves by basename across the whole Public/ or Private/ tree, but the
    plugin keeps headers flat, so a top-level scan matches reality)."""
    names = set()
    for root, _dirs, files in os.walk(directory):
        for f in files:
            if f.endswith(".h"):
                names.add(f)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--module", default=DEFAULT_MODULE,
                        help="module dir containing Public/ and Private/ "
                             "(default: %(default)s)")
    args = parser.parse_args()

    module_dir = args.module if os.path.isabs(args.module) else os.path.join(REPO, args.module)
    public_dir = os.path.join(module_dir, "Public")
    private_dir = os.path.join(module_dir, "Private")

    if not os.path.isdir(public_dir):
        print(f"error: no Public/ dir at {public_dir}", file=sys.stderr)
        return 2

    public_headers = header_names(public_dir)
    private_headers = header_names(private_dir) if os.path.isdir(private_dir) else set()

    # A private-only header: exists in Private/, not in Public/.
    private_only = private_headers - public_headers

    violations = []
    for root, _dirs, files in os.walk(public_dir):
        for f in sorted(files):
            if not f.endswith(".h"):
                continue
            path = os.path.join(root, f)
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            for inc in INCLUDE_RE.findall(text):
                base = os.path.basename(inc)
                if base.endswith(GENERATED_SUFFIX):
                    continue
                if base in private_only:
                    violations.append((os.path.relpath(path, REPO), inc))

    if violations:
        print("Public headers include private-only headers (would break external "
              "C++ consumers with C1083):\n", file=sys.stderr)
        for pub, inc in violations:
            print(f"  {pub}  ->  {inc}", file=sys.stderr)
        print("\nFix: move the included header into Public/, or forward-declare in "
              "the public header and include it in the .cpp only.", file=sys.stderr)
        return 1

    print(f"OK: {len(public_headers)} public headers, none reach into Private/.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

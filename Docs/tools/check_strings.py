"""Checks the interface string files (romfs/gfx/msg/string_resources_*.txt) against the source code.

Reports, per language : strings used in the code (LOCALIZED(KEY)) but missing from the file (shown in the app as
"[SR Not Found]"), strings in the file that the code no longer uses, and the file size (the loader reads at most
16 KB, see source/data_io/string_resource.cpp).
Usage (from the repository root, no dependencies) : python Docs/tools/check_strings.py
"""
import glob
import os
import re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
MAX_SIZE = 0x4000


def main():
    used = set()
    for path in glob.glob(os.path.join(ROOT, "source", "**", "*.[ch]pp"), recursive=True):
        with open(path, encoding="utf-8", errors="replace") as f:
            used |= set(re.findall(r"LOCALIZED\((\w+)\)", f.read()))
    used |= {"ENABLED", "DISABLED"}  # LOCALIZED_ENABLED_STATUS()
    used.discard("id")  # the macro definition itself

    ok = True
    for path in sorted(glob.glob(os.path.join(ROOT, "romfs", "gfx", "msg", "string_resources_*.txt"))):
        with open(path, encoding="utf-8-sig") as f:
            keys = set(re.findall(r"^<(\w+)>", f.read(), re.MULTILINE))
        size = os.path.getsize(path)
        missing, unused = sorted(used - keys), sorted(keys - used)
        print("%s : %d bytes%s" % (os.path.basename(path), size, " (TOO LARGE)" if size >= MAX_SIZE else ""))
        if missing:
            print("  missing :", ", ".join(missing))
        if unused:
            print("  unused  :", ", ".join(unused))
        ok = ok and not missing and size < MAX_SIZE
    print("OK" if ok else "Problems found")


if __name__ == "__main__":
    main()

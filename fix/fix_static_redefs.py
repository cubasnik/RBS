#!/usr/bin/env python3
"""
fix/fix_static_redefs.py
Fixes duplicate static function definitions and duplicate static variable
initializations in asn1c-generated C files.
MSVC (C2084, C2374) doesn't allow redefinitions even for static symbols.
We rename each duplicate occurrence by appending a counter suffix.

Путь к корню проекта вычисляется относительно расположения скрипта (fix/..),
поэтому скрипт работает корректно из любого рабочего каталога.
"""
import re
import sys
import os

# Корень проекта = родительская директория папки fix/
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fix_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # ── Find duplicate static function names ─────────────────────────────────
    static_fn_re = re.compile(
        r'(^static\s+\S+\s*\n(\w+)\s*\()',
        re.MULTILINE
    )
    fn_counts = {}
    for m in static_fn_re.finditer(content):
        name = m.group(2)
        fn_counts[name] = fn_counts.get(name, 0) + 1

    duplicates = {name for name, cnt in fn_counts.items() if cnt > 1}

    if not duplicates:
        # Check static variable redefinitions (C2374)
        static_var_re = re.compile(
            r'\b(static\s+(?:const\s+)?\w+(?:\s+\w+)?\s+(\w+)\s*(?:\[|\=))',
            re.MULTILINE
        )
        var_counts = {}
        for m in static_var_re.finditer(content):
            name = m.group(2)
            var_counts[name] = var_counts.get(name, 0) + 1
        duplicates = {name for name, cnt in var_counts.items() if cnt > 1}

        if not duplicates:
            print(f"  No duplicates found in {os.path.basename(path)}")
            return

    print(f"  Found {len(duplicates)} duplicate static names in "
          f"{os.path.basename(path)}: {list(duplicates)[:5]}...")

    new_content = content
    for name in sorted(duplicates):
        counter = [0]

        def replace_occurrence(m, _counter=counter, _name=name):
            _counter[0] += 1
            if _counter[0] == 1:
                return m.group(0)
            new_name = f"{_name}_{_counter[0]}"
            return m.group(0).replace(_name, new_name, 1)

        # multi-line: "static <type>\nNAME("
        fn_def_re = re.compile(
            r'(?m)(^static\s+\S+\s*\n)(' + re.escape(name) + r')(\s*\()',
        )
        new_content = fn_def_re.sub(replace_occurrence, new_content)

        # single-line: "static int NAME("
        counter[0] = 0
        fn_single_re = re.compile(
            r'(?m)(^static\s+\S+\s+)(' + re.escape(name) + r')(\s*\()',
        )
        fn_count_so_far = sum(1 for _ in fn_def_re.finditer(content))
        counter[0] = fn_count_so_far
        new_content = fn_single_re.sub(replace_occurrence, new_content)

    # ── Duplicate static variables (C2374) ────────────────────────────────────
    static_var_full_re = re.compile(
        r'(?m)(^static\s+(?:const\s+)?\w+(?:\s+\w+)?\s+)(\w+)(\s*(?:\[|=|\{))'
    )
    var_counts2 = {}
    for m in static_var_full_re.finditer(new_content):
        name = m.group(2)
        var_counts2[name] = var_counts2.get(name, 0) + 1
    var_dups = {name for name, cnt in var_counts2.items() if cnt > 1}

    if var_dups:
        print(f"    Also renaming {len(var_dups)} duplicate static variables")
        var_counter = {}

        def replace_var_occurrence(m, _var_dups=var_dups, _var_counter=var_counter):
            name = m.group(2)
            if name not in _var_dups:
                return m.group(0)
            _var_counter[name] = _var_counter.get(name, 0) + 1
            if _var_counter[name] == 1:
                return m.group(0)
            new_name = f"{name}_{_var_counter[name]}"
            return m.group(1) + new_name + m.group(3)

        new_content = static_var_full_re.sub(replace_var_occurrence, new_content)

    with open(path, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print(f"  Fixed: {path}")


if __name__ == '__main__':
    files = sys.argv[1:] if len(sys.argv) > 1 else []
    if not files:
        s1ap_dir = os.path.join(ROOT, 'src', 'generated', 's1ap')
        files = [
            os.path.join(s1ap_dir, 'ProtocolIE-Field.c'),
            os.path.join(s1ap_dir, 'ProtocolExtensionField.c'),
        ]
    for f in files:
        if os.path.exists(f):
            print(f"Processing {f}...")
            fix_file(f)
        else:
            print(f"File not found: {f}")

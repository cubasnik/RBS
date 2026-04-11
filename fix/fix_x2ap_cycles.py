"""
fix/fix_x2ap_cycles.py
Fix circular include chains in X2AP generated headers.

The problem: ProtocolExtensionField.h includes RSRPMRList.h and CSIReportList.h
(used by value in unions). These types transitively pull in ProtocolExtensionContainer.h
which pulls ProtocolExtensionField.h back = cycle, causing undefined types.

Fix: Remove #include "ProtocolExtensionContainer.h" from the Referred section
of headers that are in the transitive include chain of RSRPMRList.h / CSIReportList.h.

Also fix: asn_ioc.h missing aioc__undefined
Also fix: asn_system.h missing <inttypes.h> in MSVC branch
Also fix: empty unions (C2016) in ProtocolExtensionField.h

Путь к корню проекта вычисляется относительно расположения скрипта (fix/..),
поэтому скрипт работает корректно из любого рабочего каталога.
"""
import os
import re

# Корень проекта = родительская директория папки fix/
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
X2AP = os.path.join(ROOT, 'src', 'generated', 'x2ap')


def _read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def _write(path, text):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def remove_referred_protextcontainer(filename):
    """Remove #include "ProtocolExtensionContainer.h" from Referred section."""
    path = os.path.join(X2AP, filename)
    text = _read(path)
    original = text
    text = re.sub(r'\n#include "ProtocolExtensionContainer\.h"\n', '\n', text)
    if text != original:
        _write(path, text)
        print(f"  Fixed: {filename} - removed ProtocolExtensionContainer.h from Referred")
        return True
    print(f"  SKIP: {filename} - pattern not found")
    return False


def fix_asn_ioc():
    """Add aioc__undefined to x2ap/asn_ioc.h"""
    path = os.path.join(X2AP, 'asn_ioc.h')
    if not os.path.exists(path):
        print("  SKIP: asn_ioc.h not found")
        return False
    text = _read(path)
    if 'aioc__undefined' in text:
        print("  SKIP: asn_ioc.h - aioc__undefined already present")
        return False
    text = text.replace(
        '    enum {\n        aioc__value,',
        '    enum {\n        aioc__undefined,\n        aioc__value,'
    )
    _write(path, text)
    print("  Fixed: asn_ioc.h - added aioc__undefined")
    return True


def fix_asn_system():
    """Add <inttypes.h> to x2ap/asn_system.h in MSVC branch"""
    path = os.path.join(X2AP, 'asn_system.h')
    if not os.path.exists(path):
        print("  SKIP: x2ap/asn_system.h not found (may share with s1ap)")
        return False
    text = _read(path)
    if 'inttypes.h' in text:
        print("  SKIP: asn_system.h - inttypes.h already present")
        return False
    original = text
    text = text.replace('#include <float.h>', '#include <float.h>\n#include <inttypes.h>')
    if text != original:
        _write(path, text)
        print("  Fixed: asn_system.h - added <inttypes.h>")
        return True
    print("  SKIP: asn_system.h - pattern not found")
    return False


def fix_all_empty_unions():
    """Fix empty unions (C2016) in all x2ap .h files"""
    if not os.path.isdir(X2AP):
        print(f"  SKIP: X2AP directory not found: {X2AP}")
        return
    total = 0
    for fn in sorted(os.listdir(X2AP)):
        if not fn.endswith('.h'):
            continue
        path = os.path.join(X2AP, fn)
        text = _read(path)
        original = text
        new_text = re.sub(
            r'([ \t]+union\s+\w*\s*\{)([ \t]*\n[ \t]+\}[ \t]*(?:choice|present_u)\s*;)',
            lambda m: (m.group(1) + '\n\t\t\tchar _msvc_dummy_;\n' + m.group(2)
                       if '_msvc_dummy_' not in m.group(0) else m.group(0)),
            text
        )
        if new_text != text:
            _write(path, new_text)
            count = new_text.count('_msvc_dummy_') - text.count('_msvc_dummy_')
            total += count
            print(f"  Fixed union: {fn} (+{count})")
    print(f"  Total empty unions fixed: {total}")


def get_direct_includes(filename):
    path = os.path.join(X2AP, filename)
    if not os.path.exists(path):
        return []
    return re.findall(r'#include "([^"]+)"', _read(path))


def bfs_includes(start_files):
    visited, queue = set(), list(start_files)
    while queue:
        fn = queue.pop(0)
        if fn in visited:
            continue
        visited.add(fn)
        for inc in get_direct_includes(fn):
            if inc not in visited and os.path.exists(os.path.join(X2AP, inc)):
                queue.append(inc)
    return visited


def run():
    if not os.path.isdir(X2AP):
        print(f"X2AP directory not found: {X2AP}")
        return

    print("=" * 60)
    print("Fix 1: Remove ProtExtContainer from Referred sections (cycle break)")
    print("=" * 60)

    transitive_of_rsr = bfs_includes(['RSRPMRList.h', 'CSIReportList.h'])
    print(f"Transitive includes of RSRPMRList+CSIReportList: {len(transitive_of_rsr)} files")

    to_fix = [
        fn for fn in sorted(transitive_of_rsr)
        if os.path.exists(os.path.join(X2AP, fn))
        and '#include "ProtocolExtensionContainer.h"' in _read(os.path.join(X2AP, fn))
    ]
    print(f"Files with ProtExtContainer that need fixing: {len(to_fix)}")
    for fn in to_fix:
        print(f"  - {fn}")

    print("\nApplying fixes...")
    fixed = sum(1 for fn in to_fix if remove_referred_protextcontainer(fn))
    print(f"Fixed {fixed} files")

    print("\n" + "=" * 60)
    print("Fix 2: asn_ioc.h - aioc__undefined")
    print("=" * 60)
    fix_asn_ioc()

    print("\n" + "=" * 60)
    print("Fix 3: asn_system.h - <inttypes.h>")
    print("=" * 60)
    fix_asn_system()

    print("\n" + "=" * 60)
    print("Fix 4: Empty unions C2016 in x2ap headers")
    print("=" * 60)
    fix_all_empty_unions()

    print("\nDone!")


if __name__ == '__main__':
    run()

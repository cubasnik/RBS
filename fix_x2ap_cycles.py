"""
Fix circular include chains in X2AP generated headers.

The problem: ProtocolExtensionField.h includes RSRPMRList.h and CSIReportList.h
(used by value in unions). These types transitively pull in ProtocolExtensionContainer.h
which pulls ProtocolExtensionField.h back = cycle, causing undefined types.

Fix: Remove #include "ProtocolExtensionContainer.h" from the Referred section
of headers that are in the transitive include chain of RSRPMRList.h / CSIReportList.h.

Also fix: asn_ioc.h missing aioc__undefined
Also fix: asn_system.h missing <inttypes.h> in MSVC branch
Also fix: empty unions (C2016) in ProtocolExtensionField.h
"""

import os
import re

X2AP = r'C:\Users\Alexey\Desktop\min\vNE\RBS\RBS\src\generated\x2ap'

def read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()

def write(path, text):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)

def remove_referred_protextcontainer(filename):
    """Remove #include "ProtocolExtensionContainer.h" from Referred section of a header."""
    path = os.path.join(X2AP, filename)
    text = read(path)
    
    # Pattern: remove the line '#include "ProtocolExtensionContainer.h"' in Referred section
    # The Referred section is between #ifdef __cplusplus } #endif and main guard #endif
    # Just remove any occurrence of this include line
    original = text
    text = re.sub(r'\n#include "ProtocolExtensionContainer\.h"\n', '\n', text)
    
    if text != original:
        write(path, text)
        print(f"  Fixed: {filename} - removed ProtocolExtensionContainer.h from Referred")
        return True
    else:
        print(f"  SKIP: {filename} - pattern not found")
        return False


def fix_asn_ioc():
    """Add aioc__undefined to x2ap/asn_ioc.h"""
    path = os.path.join(X2AP, 'asn_ioc.h')
    text = read(path)
    if 'aioc__undefined' in text:
        print("  SKIP: asn_ioc.h - aioc__undefined already present")
        return False
    text = text.replace(
        '    enum {\n        aioc__value,',
        '    enum {\n        aioc__undefined,\n        aioc__value,'
    )
    write(path, text)
    print("  Fixed: asn_ioc.h - added aioc__undefined")
    return True


def fix_asn_system():
    """Add <inttypes.h> to x2ap/asn_system.h in MSVC branch"""
    path = os.path.join(X2AP, 'asn_system.h')
    if not os.path.exists(path):
        print("  SKIP: x2ap/asn_system.h not found (may share with s1ap)")
        return False
    text = read(path)
    if 'inttypes.h' in text:
        print("  SKIP: asn_system.h - inttypes.h already present")
        return False
    # Add after #include <float.h> in the WIN32 section
    original = text
    text = text.replace(
        '#include <float.h>\n#include <inttypes.h>',
        '#include <float.h>\n#include <inttypes.h>'
    )
    if text == original:
        text = text.replace(
            '#include <float.h>',
            '#include <float.h>\n#include <inttypes.h>'
        )
    if text != original:
        write(path, text)
        print("  Fixed: asn_system.h - added <inttypes.h>")
        return True
    print("  SKIP: asn_system.h - pattern not found")
    return False


def fix_empty_unions(filename):
    """Add char _msvc_dummy_ to empty unions (C2016)"""
    path = os.path.join(X2AP, filename)
    text = read(path)
    original = text
    
    # Pattern: union { } choice; -> union { char _msvc_dummy_; } choice;
    # Also handle: union { } choice_u; etc.
    def replace_empty_union(m):
        return m.group(0).replace(
            'union ' + m.group(1) + ' {',
            'union ' + m.group(1) + ' {\n\t\tchar _msvc_dummy_;'
        ) if '\t\tchar _msvc_dummy_;' not in m.group(0) else m.group(0)
    
    # Simple approach: find empty union bodies
    # Match: \tunion Name {\n\t} varname;
    text = re.sub(
        r'(\t(?:union|struct)\s+\w*\s*\{)\s*\n\s*(\}\s*\w+;)',
        lambda m: m.group(1) + '\n\t\tchar _msvc_dummy_;\n\t' + m.group(2).lstrip(),
        text
    )
    
    if text != original:
        write(path, text)
        count = text.count('_msvc_dummy_') - original.count('_msvc_dummy_')
        print(f"  Fixed: {filename} - added _msvc_dummy_ to {count} empty unions")
        return True
    return False


def fix_all_empty_unions():
    """Fix empty unions in all x2ap .h files"""
    total = 0
    for fn in sorted(os.listdir(X2AP)):
        if not fn.endswith('.h'):
            continue
        path = os.path.join(X2AP, fn)
        text = read(path)
        original = text
        
        # Find empty union {} blocks inside typedef/struct
        # Pattern in asn1c output: 
        #   \t\tunion SomeName_u {\n\t\t} choice;
        #  or just:
        #   union {\n\t} choice;
        new_text = re.sub(
            r'([ \t]+union\s+\w*\s*\{)([ \t]*\n[ \t]+\}[ \t]*(?:choice|present_u)\s*;)',
            lambda m: m.group(1) + '\n\t\t\tchar _msvc_dummy_;\n' + m.group(2)
            if '_msvc_dummy_' not in m.group(0) else m.group(0),
            text
        )
        if new_text != text:
            write(path, new_text)
            count = new_text.count('_msvc_dummy_') - text.count('_msvc_dummy_')
            total += count
            print(f"  Fixed union: {fn} (+{count})")
    print(f"  Total empty unions fixed: {total}")


print("=" * 60)
print("Fix 1: Remove ProtExtContainer from Referred sections (cycle break)")
print("=" * 60)
# Files whose Referred sections create cycles via ProtExtField.h
cyclic_files = [
    'ECGI.h',
    'RSRPMeasurementResult.h',
    'CSIReportPerCSIProcess.h',
    'CSIReportPerCSIProcessItem.h',
]

# Also check all files transitively included by RSRPMRList.h and CSIReportList.h
# Run BFS to find transitive includes
def get_direct_includes(filename):
    path = os.path.join(X2AP, filename)
    if not os.path.exists(path):
        return []
    text = read(path)
    # Only local includes (no angle brackets)
    return re.findall(r'#include "([^"]+)"', text)

def bfs_includes(start_files):
    visited = set()
    queue = list(start_files)
    while queue:
        fn = queue.pop(0)
        if fn in visited:
            continue
        visited.add(fn)
        for inc in get_direct_includes(fn):
            if inc not in visited and os.path.exists(os.path.join(X2AP, inc)):
                queue.append(inc)
    return visited

transitive_of_rsr = bfs_includes(['RSRPMRList.h', 'CSIReportList.h'])
print(f"Transitive includes of RSRPMRList+CSIReportList: {len(transitive_of_rsr)} files")

# Find which of these have ProtExtContainer in their Referred section
# Referred section = between last #ifdef __cplusplus } #endif and main guard #endif
to_fix = []
for fn in sorted(transitive_of_rsr):
    path = os.path.join(X2AP, fn)
    if not os.path.exists(path):
        continue
    text = read(path)
    if '#include "ProtocolExtensionContainer.h"' in text:
        to_fix.append(fn)

print(f"Files with ProtExtContainer that need fixing: {len(to_fix)}")
for fn in to_fix:
    print(f"  - {fn}")

print()
print("Applying fixes...")
fixed = 0
for fn in to_fix:
    if remove_referred_protextcontainer(fn):
        fixed += 1
print(f"Fixed {fixed} files")

print()
print("=" * 60)
print("Fix 2: asn_ioc.h - aioc__undefined")
print("=" * 60)
fix_asn_ioc()

print()
print("=" * 60)
print("Fix 3: asn_system.h - <inttypes.h>")
print("=" * 60)
fix_asn_system()

print()
print("=" * 60)
print("Fix 4: Empty unions C2016 in x2ap headers")
print("=" * 60)
fix_all_empty_unions()

print()
print("Done!")

#!/usr/bin/env python3
"""
fix/fix_protocol_ie_field.py
Smart fix for asn1c ProtocolIE-Field.c:
Renames duplicate static symbols by block index.
Each "block" = content before each asn_TYPE_descriptor_t definition.

Путь к корню проекта вычисляется относительно расположения скрипта (fix/..),
поэтому скрипт работает корректно из любого рабочего каталога.
"""
import re
import os
import sys
from collections import Counter

# Корень проекта = родительская директория папки fix/
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fix_protocol_ie_field(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()

    orig_len = len(text)

    fn_names = re.findall(r'(?m)^static\s+\w+\n(\w+)\s*\(', text)
    var_names = re.findall(
        r'(?m)^(?:static|#if[^\n]*\nstatic)\s+(?:const\s+)?'
        r'(?:asn_per_constraints_t|asn_TYPE_member_t|asn_MAP_tag2element_t|'
        r'asn_TYPE_tag2member_t|asn_CHOICE_specifics_t|asn_SEQUENCE_specifics_t|'
        r'asn_SET_OF_specifics_t|asn_TYPE_descriptor_t)\s+(\w+)',
        text
    )

    dup_fns  = {n for n, c in Counter(fn_names).items()  if c > 1}
    dup_vars = {n for n, c in Counter(var_names).items() if c > 1}

    print(f"Duplicate functions: {dup_fns}")
    print(f"Duplicate variables: {dup_vars}")

    all_dups = dup_fns | dup_vars
    if not all_dups:
        print("No duplicates found!")
        return

    td_re = re.compile(r'^asn_TYPE_descriptor_t\s+asn_DEF_\w+\s*=', re.MULTILINE)
    block_starts = [0] + [m.start() for m in td_re.finditer(text)]
    block_starts.append(len(text))

    print(f"Processing {len(block_starts)-1} blocks...")

    blocks = [text[block_starts[i]:block_starts[i+1]]
              for i in range(len(block_starts) - 1)]

    name_counters = {n: 0 for n in all_dups}
    new_blocks = []

    for block in blocks:
        new_block = block
        for dup_name in sorted(all_dups):
            if not re.search(r'\b' + re.escape(dup_name) + r'\b', block):
                continue
            name_counters[dup_name] += 1
            block_count = name_counters[dup_name]
            if block_count == 1:
                continue
            new_name  = f"{dup_name}_{block_count}"
            new_block = re.sub(r'\b' + re.escape(dup_name) + r'\b', new_name, new_block)
        new_blocks.append(new_block)

    new_text = ''.join(new_blocks)

    # Verify
    fn_names2  = re.findall(r'(?m)^static\s+\w+\n(\w+)\s*\(', new_text)
    var_names2 = re.findall(
        r'(?m)^(?:static|#if[^\n]*\nstatic)\s+(?:const\s+)?'
        r'(?:asn_per_constraints_t|asn_TYPE_member_t|asn_MAP_tag2element_t|'
        r'asn_TYPE_tag2member_t|asn_CHOICE_specifics_t|asn_SEQUENCE_specifics_t|'
        r'asn_SET_OF_specifics_t|asn_TYPE_descriptor_t)\s+(\w+)',
        new_text
    )
    remaining_fns  = {n for n, c in Counter(fn_names2).items()  if c > 1}
    remaining_vars = {n for n, c in Counter(var_names2).items() if c > 1}

    if remaining_fns or remaining_vars:
        print(f"WARNING: Still have duplicates! fns={remaining_fns}, vars={remaining_vars}")
    else:
        print("All duplicates resolved!")

    with open(path, 'w', encoding='utf-8') as f:
        f.write(new_text)
    print(f"Saved {path}: {orig_len} -> {len(new_text)} chars")


if __name__ == '__main__':
    default_path = os.path.join(ROOT, 'src', 'generated', 's1ap', 'ProtocolIE-Field.c')
    path = sys.argv[1] if len(sys.argv) > 1 else default_path
    fix_protocol_ie_field(path)

#!/usr/bin/env python3
"""
Smart fix for asn1c ProtocolIE-Field.c:
Renames duplicate static symbols by block index.
Each "block" = content before each asn_TYPE_descriptor_t definition.
"""
import re
import os
import sys
from collections import Counter


def fix_protocol_ie_field(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    
    orig_len = len(text)
    
    # Find all duplicate static symbol names (functions and variables)
    # Pattern 1: static functions "static int\nfoo_name("  
    fn_names = re.findall(r'(?m)^static\s+\w+\n(\w+)\s*\(', text)
    # Pattern 2: static variables (various types) "static [const] TYPE foo_name"
    var_names = re.findall(r'(?m)^(?:static|#if[^\n]*\nstatic)\s+(?:const\s+)?(?:asn_per_constraints_t|asn_TYPE_member_t|asn_MAP_tag2element_t|asn_TYPE_tag2member_t|asn_CHOICE_specifics_t|asn_SEQUENCE_specifics_t|asn_SET_OF_specifics_t|asn_TYPE_descriptor_t)\s+(\w+)', text)
    
    fn_counts = Counter(fn_names)
    var_counts = Counter(var_names)
    
    dup_fns = {n for n, c in fn_counts.items() if c > 1}
    dup_vars = {n for n, c in var_counts.items() if c > 1}
    
    print(f"Duplicate functions: {dup_fns}")
    print(f"Duplicate variables: {dup_vars}")
    
    all_dups = dup_fns | dup_vars
    if not all_dups:
        print("No duplicates found!")
        return
    
    # Find positions of block boundaries (asn_TYPE_descriptor_t asn_DEF_XXX =)
    td_re = re.compile(r'^asn_TYPE_descriptor_t\s+asn_DEF_\w+\s*=', re.MULTILINE)
    block_starts = [0] + [m.start() for m in td_re.finditer(text)]
    block_starts.append(len(text))
    
    print(f"Processing {len(block_starts)-1} blocks...")
    
    # Process each block
    new_text = text
    
    # Process in reverse to preserve positions
    # Actually we need to build new_text block by block
    blocks = []
    for i in range(len(block_starts) - 1):
        block = text[block_starts[i]:block_starts[i+1]]
        blocks.append(block)
    
    # For each duplicate name, track which occurrence we're in
    name_counters = {n: 0 for n in all_dups}
    new_blocks = []
    
    for block_idx, block in enumerate(blocks):
        new_block = block
        for dup_name in sorted(all_dups):
            # Find all occurrences in this block
            count_in_block = len(re.findall(r'\b' + re.escape(dup_name) + r'\b', block))
            if count_in_block == 0:
                continue
            
            name_counters[dup_name] += 1
            block_count = name_counters[dup_name]
            
            if block_count == 1:
                # First occurrence - keep as-is
                continue
            
            # Rename all occurrences in this block
            new_name = f"{dup_name}_{block_count}"
            new_block = re.sub(r'\b' + re.escape(dup_name) + r'\b', new_name, new_block)
        
        new_blocks.append(new_block)
    
    new_text = ''.join(new_blocks)
    
    # Verify duplicates are gone
    fn_names2 = re.findall(r'(?m)^static\s+\w+\n(\w+)\s*\(', new_text)
    var_names2 = re.findall(r'(?m)^(?:static|#if[^\n]*\nstatic)\s+(?:const\s+)?(?:asn_per_constraints_t|asn_TYPE_member_t|asn_MAP_tag2element_t|asn_TYPE_tag2member_t|asn_CHOICE_specifics_t|asn_SEQUENCE_specifics_t|asn_SET_OF_specifics_t|asn_TYPE_descriptor_t)\s+(\w+)', new_text)
    remaining_fns = {n for n, c in Counter(fn_names2).items() if c > 1}
    remaining_vars = {n for n, c in Counter(var_names2).items() if c > 1}
    
    if remaining_fns or remaining_vars:
        print(f"WARNING: Still have duplicates! fns={remaining_fns}, vars={remaining_vars}")
    else:
        print("All duplicates resolved!")
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(new_text)
    print(f"Saved {path}: {orig_len} -> {len(new_text)} chars")


if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\Alexey\Desktop\min\vNE\RBS\RBS\src\generated\s1ap\ProtocolIE-Field.c'
    fix_protocol_ie_field(path)

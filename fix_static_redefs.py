#!/usr/bin/env python3
"""
Fixes duplicate static function definitions and duplicate static variable initializations
in asn1c-generated C files. MSVC (C2084, C2374) doesn't allow redefinitions even for
static symbols. We rename each duplicate occurrence by appending a counter suffix.
"""
import re
import sys
import os

def fix_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # Track occurrence counts for static function definitions
    # Pattern: "static int\nfunc_name(" or "static <type>\nfunc_name("
    # Also handles "static const <type> varname[] = {"

    lines = content.split('\n')
    
    # Pass 1: find all static function and variable names and their line positions
    # static function: "static <returntype>\nfoo_name(..."
    # static variable: "static const <type> foo_name[" or "static <type> foo_name ="
    
    # Build map: name -> list of line indices where definition starts
    func_def_pattern = re.compile(
        r'^(static\s+(?:const\s+)?(?:int|unsigned|void|long|char|asn_per_constraints_t|asn_TYPE_member_t|asn_TYPE_descriptor_t|asn_SET_OF_specifics_t|asn_SEQUENCE_specifics_t|asn_CHOICE_specifics_t|asn_ioc_table_t|asn_ioc_cell_t|ber_tlv_tag_t|asn_TYPE_operation_t|asn_MAP_tag2element_t|asn_MAP_element_s|asn_ioc_row_t)(?:\s+\*)?)\s*$'
    )
    
    # Simpler approach: find "static <something>\nNAME(" 
    # where static fn definitions are split across two lines
    
    # Even simpler: just use regex on full content
    
    # Pattern for static function definitions:
    # "static int\nmemb_id_constraint_0(" etc.
    static_fn_re = re.compile(
        r'(^static\s+\S+\s*\n(\w+)\s*\()',
        re.MULTILINE
    )
    
    # Count occurrences of each function name
    fn_counts = {}
    for m in static_fn_re.finditer(content):
        name = m.group(2)
        fn_counts[name] = fn_counts.get(name, 0) + 1

    # Find duplicates
    duplicates = {name for name, cnt in fn_counts.items() if cnt > 1}
    
    if not duplicates:
        # Also check static variable redefinitions (C2374)
        # Pattern: static const TYPE varname[
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

    print(f"  Found {len(duplicates)} duplicate static names in {os.path.basename(path)}: {list(duplicates)[:5]}...")

    # Replace duplicates: for each name, replace 2nd, 3rd... occurrence with name_2, name_3...
    new_content = content
    for name in sorted(duplicates):
        counter = [0]
        
        def replace_occurrence(m):
            counter[0] += 1
            if counter[0] == 1:
                return m.group(0)  # Keep first occurrence as-is
            # Replace name with name_N in this definition
            new_name = f"{name}_{counter[0]}"
            return m.group(0).replace(name, new_name, 1)
        
        # Match static function definitions: "static <type>\nNAME("
        # We need to replace just the function definition, not all uses
        fn_def_re = re.compile(
            r'(?m)(^static\s+\S+\s*\n)(' + re.escape(name) + r')(\s*\()',
        )
        new_content = fn_def_re.sub(replace_occurrence, new_content)
        
        # Also match single-line static function: "static int NAME("
        counter[0] = 0
        fn_single_re = re.compile(
            r'(?m)(^static\s+\S+\s+)(' + re.escape(name) + r')(\s*\()',
        )
        # Reset counter to what we found above to continue numbering
        fn_count_so_far = sum(1 for _ in fn_def_re.finditer(content))
        counter[0] = fn_count_so_far
        new_content = fn_single_re.sub(replace_occurrence, new_content)
    
    # Now fix static variable duplicates (C2374)
    # Pattern: static const TYPE varname[ = {
    static_var_full_re = re.compile(
        r'(?m)(^static\s+(?:const\s+)?\w+(?:\s+\w+)?\s+)(\w+)(\s*(?:\[|=|\{))'
    )
    
    var_counter = {}
    def replace_var_occurrence(m):
        name = m.group(2)
        if name not in duplicates:
            return m.group(0)
        var_counter[name] = var_counter.get(name, 0) + 1
        if var_counter[name] == 1:
            return m.group(0)
        new_name = f"{name}_{var_counter[name]}"
        return m.group(1) + new_name + m.group(3)
    
    # Find variable duplicates in new_content
    var_counts2 = {}
    for m in static_var_full_re.finditer(new_content):
        name = m.group(2)
        var_counts2[name] = var_counts2.get(name, 0) + 1
    var_dups = {name for name, cnt in var_counts2.items() if cnt > 1}
    
    if var_dups:
        print(f"    Also renaming {len(var_dups)} duplicate static variables")
        new_content = static_var_full_re.sub(replace_var_occurrence, new_content)
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print(f"  Fixed: {path}")


if __name__ == '__main__':
    files = sys.argv[1:] if len(sys.argv) > 1 else []
    if not files:
        # Default: fix all known problematic files
        base = r'C:\Users\Alexey\Desktop\min\vNE\RBS\RBS\src\generated\s1ap'
        files = [
            os.path.join(base, 'ProtocolIE-Field.c'),
            os.path.join(base, 'ProtocolExtensionField.c'),
        ]
    for f in files:
        if os.path.exists(f):
            print(f"Processing {f}...")
            fix_file(f)
        else:
            print(f"File not found: {f}")

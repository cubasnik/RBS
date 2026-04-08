"""
Fix ASN1C_NO_UNSUFFIXED_PDU_ALIAS issue in x2ap .c files.

When ASN1C_NO_UNSUFFIXED_PDU_ALIAS is defined, the #ifndef block is skipped,
but asn_MBR_ tables still reference the unsuffixed asn_DEF_XXX names.

Fix: after each #endif /* ASN1C_NO_UNSUFFIXED_PDU_ALIAS */ block, add:
  #ifdef ASN1C_NO_UNSUFFIXED_PDU_ALIAS
  #define asn_DEF_XXX asn_DEF_XXX_N
  #endif
"""
import os, re

X2AP = r'C:\Users\Alexey\Desktop\min\vNE\RBS\RBS\src\generated\x2ap'

# Pattern to match a #ifndef ASN1C_NO_UNSUFFIXED_PDU_ALIAS block
BLOCK_PAT = re.compile(
    r'(#ifndef ASN1C_NO_UNSUFFIXED_PDU_ALIAS\b.*?#endif\s*/\*\s*ASN1C_NO_UNSUFFIXED_PDU_ALIAS\s*\*/)',
    re.DOTALL
)

# Pattern to extract alias from block: either ELF or non-ELF variant
# ELF: alias("asn_DEF_XXX_N") for declaration asn_DEF_XXX
ALIAS_ELF = re.compile(
    r'extern asn_TYPE_descriptor_t\s+(asn_DEF_\w+)\s+__attribute__.*?alias\("(asn_DEF_\w+)"\)'
)
# Non-ELF constructor: asn_DEF_XXX = asn_DEF_XXX_N;
ALIAS_CTOR = re.compile(
    r'(asn_DEF_\w+)\s*=\s*(asn_DEF_\w+_\d+)\s*;'
)

fixed_total = 0

for fn in sorted(os.listdir(X2AP)):
    if not fn.endswith('.c'):
        continue
    path = os.path.join(X2AP, fn)
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    
    if 'ASN1C_NO_UNSUFFIXED_PDU_ALIAS' not in text:
        continue
    
    original = text
    
    # Find all blocks and their positions
    def process_blocks(text):
        result = []
        pos = 0
        for m in BLOCK_PAT.finditer(text):
            block = m.group(1)
            block_start = m.start()
            block_end = m.end()
            
            # Extract alias mapping from block
            aliases = {}  # unsuffixed -> suffixed
            
            # Try ELF pattern first
            for em in ALIAS_ELF.finditer(block):
                unsuf, suf = em.group(1), em.group(2)
                aliases[unsuf] = suf
            
            # Try non-ELF constructor pattern
            for cm in ALIAS_CTOR.finditer(block):
                unsuf, suf = cm.group(1), cm.group(2)
                aliases[unsuf] = suf
            
            if not aliases:
                continue
            
            # Check which aliases are actually referenced AFTER the block
            after_text = text[block_end:]
            defines_to_add = []
            for unsuf, suf in aliases.items():
                # Check if unsuffixed name is used outside the block
                if re.search(r'&\s*' + re.escape(unsuf) + r'\b', after_text):
                    defines_to_add.append((unsuf, suf))
            
            if defines_to_add:
                result.append((block_end, defines_to_add))
        
        return result
    
    insertions = process_blocks(text)
    
    if not insertions:
        continue
    
    # Apply insertions in reverse order to maintain positions
    insertions.sort(key=lambda x: x[0], reverse=True)
    for pos, defines in insertions:
        lines = []
        for unsuf, suf in defines:
            lines.append(f'#ifdef ASN1C_NO_UNSUFFIXED_PDU_ALIAS')
            lines.append(f'#define {unsuf} {suf}')
            lines.append(f'#endif')
        insert_text = '\n' + '\n'.join(lines) + '\n'
        text = text[:pos] + insert_text + text[pos:]
    
    if text != original:
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        count = len(insertions)
        fixed_total += 1
        print(f'Fixed: {fn} ({count} blocks)')

print(f'\nTotal files fixed: {fixed_total}')

"""Add #include "ProtocolExtensionContainer.h" to .c files that reference it but don't include it."""
import os, re

X2AP = r'C:\Users\Alexey\Desktop\min\vNE\RBS\RBS\src\generated\x2ap'
INCLUDE_LINE = '#include "ProtocolExtensionContainer.h"\n'
MARKER = '#include "ProtocolExtensionContainer.h"'

fixed = 0
for fn in sorted(os.listdir(X2AP)):
    if not fn.endswith('.c'):
        continue
    cpath = os.path.join(X2AP, fn)
    with open(cpath, encoding='utf-8', errors='replace') as f:
        ctext = f.read()
    
    # Check if this .c file references ProtExtContainer but doesn't include it
    if 'asn_DEF_ProtocolExtensionContainer' not in ctext:
        continue
    if MARKER in ctext:
        continue
    
    # Add include after the first #include line
    first_inc = ctext.find('#include')
    if first_inc < 0:
        continue
    end_of_line = ctext.find('\n', first_inc)
    ctext = ctext[:end_of_line+1] + INCLUDE_LINE + ctext[end_of_line+1:]
    
    with open(cpath, 'w', encoding='utf-8', newline='\n') as f:
        f.write(ctext)
    fixed += 1

print(f'Added ProtExtContainer include to {fixed} .c files')

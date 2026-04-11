"""
fix/fix_x2ap_c_includes.py
Add #include "ProtocolExtensionContainer.h" to .c files that reference it
but don't include it.

Путь к корню проекта вычисляется относительно расположения скрипта (fix/..),
поэтому скрипт работает корректно из любого рабочего каталога.
"""
import os
import re

# Корень проекта = родительская директория папки fix/
ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
X2AP  = os.path.join(ROOT, 'src', 'generated', 'x2ap')

INCLUDE_LINE = '#include "ProtocolExtensionContainer.h"\n'
MARKER       = '#include "ProtocolExtensionContainer.h"'


def run():
    if not os.path.isdir(X2AP):
        print(f"X2AP directory not found: {X2AP}")
        return

    fixed = 0
    for fn in sorted(os.listdir(X2AP)):
        if not fn.endswith('.c'):
            continue
        cpath = os.path.join(X2AP, fn)
        with open(cpath, encoding='utf-8', errors='replace') as f:
            ctext = f.read()

        if 'asn_DEF_ProtocolExtensionContainer' not in ctext:
            continue
        if MARKER in ctext:
            continue

        first_inc = ctext.find('#include')
        if first_inc < 0:
            continue
        end_of_line = ctext.find('\n', first_inc)
        ctext = ctext[:end_of_line + 1] + INCLUDE_LINE + ctext[end_of_line + 1:]

        with open(cpath, 'w', encoding='utf-8', newline='\n') as f:
            f.write(ctext)
        fixed += 1

    print(f'Added ProtExtContainer include to {fixed} .c files')


if __name__ == '__main__':
    run()

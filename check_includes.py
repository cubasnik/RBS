import re

s1ap_dir = r'C:\Users\Alexey\Desktop\min\vNE\RBS\RBS\src\generated\s1ap'

files_to_check = [
    'ProtocolIE-Field.h',
    'ProtocolIE-SingleContainer.h',
    'MDTMode-Extension.h',
    'SONInformation-Extension.h',
    'ProtocolExtensionField.h',
]

for fname in files_to_check:
    fpath = s1ap_dir + '\\' + fname
    with open(fpath, encoding='utf-8') as f:
        text = f.read()
    includes = re.findall(r'#include "([^"]+)"', text)
    print(f'\n{fname}:')
    for inc in includes:
        print(f'  -> {inc}')

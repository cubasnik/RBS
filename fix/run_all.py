"""
fix/run_all.py
Orchestrator: runs all post-generation fix scripts in the correct order.

Full ASN.1 workflow:
  1. python fix/fix_asn1_encoding.py    # fix .asn spec files (non-ASCII)
  2. bash gen_asn1.sh                   # generate C sources via asn1c
  3. python fix/run_all.py             # fix all generated C sources (this script)
  4. cmake --build build               # compile

Or, to run only the pre-generation encoding fix:
  python fix/fix_asn1_encoding.py

Путь к корню проекта вычисляется относительно расположения скрипта (fix/..),
поэтому скрипт работает корректно из любого рабочего каталога.
"""
import os
import sys

# Ensure imports resolve from the fix/ directory itself
_FIX_DIR = os.path.dirname(os.path.abspath(__file__))
if _FIX_DIR not in sys.path:
    sys.path.insert(0, _FIX_DIR)

ROOT = os.path.dirname(_FIX_DIR)

import fix_x2ap_c_includes
import fix_x2ap_cycles
import fix_x2ap_pdu_alias
from fix_protocol_ie_field import fix_protocol_ie_field
from fix_static_redefs import fix_file as fix_static_redefs
import fix_msvc_asn1


def _section(title):
    print()
    print('=' * 60)
    print(title)
    print('=' * 60)


def run():
    _section('Step 1/6: X2AP — missing/wrong #include directives')
    fix_x2ap_c_includes.run()

    _section('Step 2/6: X2AP — circular include chains')
    fix_x2ap_cycles.run()

    _section('Step 3/6: X2AP — PDU alias (#define) insertion')
    fix_x2ap_pdu_alias.run()

    _section('Step 4/6: S1AP — ProtocolIE-Field.c duplicate static symbols')
    for subdir in ('s1ap', 'x2ap'):
        ie_field = os.path.join(ROOT, 'src', 'generated', subdir, 'ProtocolIE-Field.c')
        if os.path.exists(ie_field):
            print(f'\n  Processing {subdir}/ProtocolIE-Field.c')
            fix_protocol_ie_field(ie_field)
        else:
            print(f'  SKIP: {ie_field} not found')

    _section('Step 5/6: S1AP — other duplicate static definitions (C2084/C2374)')
    for fname in ('ProtocolIE-Field.c', 'ProtocolExtensionField.c'):
        path = os.path.join(ROOT, 'src', 'generated', 's1ap', fname)
        if os.path.exists(path):
            print(f'\n  Processing s1ap/{fname}')
            fix_static_redefs(path)
        else:
            print(f'  SKIP: {path} not found')

    _section('Step 6/6: MSVC-specific compile errors in generated C')
    fix_msvc_asn1.run()

    print()
    print('All post-generation fixes complete.')
    print('You can now run: cmake --build build')


if __name__ == '__main__':
    run()

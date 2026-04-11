"""
fix/fix_msvc_asn1.py
Пост-обработка сгенерированных asn1c файлов для MSVC:
1. Пустые union { } choice; → добавляем char _msvc_dummy_;
2. Пустые struct { } ...;   → аналогично

Путь к корню проекта вычисляется относительно расположения скрипта (fix/..),
поэтому скрипт работает корректно из любого рабочего каталога.
"""
import os
import re

# Корень проекта = родительская директория папки fix/
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DIRS = [
    os.path.join(ROOT, "src", "generated", "s1ap"),
    os.path.join(ROOT, "src", "generated", "x2ap"),
    os.path.join(ROOT, "src", "generated", "nbap"),
]

EMPTY_UNION = re.compile(
    r'(union\s+\w+\s*\{)\s*\n(\s*\})',
    re.MULTILINE
)
EMPTY_STRUCT = re.compile(
    r'(struct\s+\w+\s*\{)\s*\n(\s*\})',
    re.MULTILINE
)


def _fix_empty_body(m):
    prefix = m.group(1)
    suffix = m.group(2)
    indent = re.match(r'\s*', suffix).group()
    return f"{prefix}\n{indent}\tchar _msvc_dummy_;\n{suffix}"


def run():
    fixed_files = 0

    for folder in DIRS:
        if not os.path.isdir(folder):
            print(f"Пропуск (не найдено): {folder}")
            continue
        for fname in os.listdir(folder):
            if not (fname.endswith('.h') or fname.endswith('.c')):
                continue
            fpath = os.path.join(folder, fname)
            with open(fpath, 'r', encoding='utf-8') as f:
                text = f.read()
            original = text

            text = EMPTY_UNION.sub(_fix_empty_body, text)
            text = EMPTY_STRUCT.sub(_fix_empty_body, text)

            # Add <inttypes.h> inside the _WIN32/_MSC_VER block if missing
            MSVC_WIN32_BLOCK = '#include <windows.h>\n#include <float.h>\n'
            INTTYPES_LINE = '#include <inttypes.h>   /* PRId32, PRIu32, PRIdMAX, PRIuMAX, etc. */\n'
            if fname == 'asn_system.h' and MSVC_WIN32_BLOCK in text and INTTYPES_LINE not in text:
                text = text.replace(MSVC_WIN32_BLOCK, MSVC_WIN32_BLOCK + INTTYPES_LINE)

            # Fix encoding_constraints struct initializer (designated initialisers)
            ENC_CONSTR_PATTERN = re.compile(
                r'\{\s*\n'
                r'(?:#if[^\n]*\n\s*0,\n#endif[^\n]*\n)?'
                r'(?:#if[^\n]*\n\s*(&\w+),\n#endif[^\n]*\n)'
                r'(?:#if[^\n]*\n\s*0,\n#endif[^\n]*\n)?'
                r'\s*(\w+)\s*\n'
                r'\s*\}',
                re.MULTILINE
            )

            def _fix_enc_constr(m):
                return f'{{ .per_constraints = {m.group(1)}, .general_constraints = {m.group(2)} }}'

            new_text = ENC_CONSTR_PATTERN.sub(_fix_enc_constr, text)
            if new_text != text:
                text = new_text

            ENC_CONSTR_ZERO_PER = re.compile(
                r'\{\s*\n'
                r'(?:#if[^\n]*\n\s*0,\n#endif[^\n]*\n)?'
                r'(?:#if[^\n]*\n\s*0,\n#endif[^\n]*\n)'
                r'(?:#if[^\n]*\n\s*0,\n#endif[^\n]*\n)?'
                r'\s*(\w+)\s*\n'
                r'\s*\}',
                re.MULTILINE
            )

            def _fix_enc_constr_zero_per(m):
                general = m.group(1)
                if general == '0':
                    return m.group(0)
                return f'{{ .general_constraints = {general} }}'

            new_text = ENC_CONSTR_ZERO_PER.sub(_fix_enc_constr_zero_per, text)
            if new_text != text:
                text = new_text

            if text != original:
                with open(fpath, 'w', encoding='utf-8') as f:
                    f.write(text)
                fixed_files += 1

            # asn_random_fill.c: replace POSIX random() with rand() for MSVC
            if fname == 'asn_random_fill.c' and 'random()' in text:
                patched = text.replace('random()', 'rand()')
                if patched != text:
                    with open(fpath, 'w', encoding='utf-8') as f:
                        f.write(patched)
                    fixed_files += 1

            # INTEGER.c: fix unsigned integer range encoding for ULONG_MAX upper bounds
            if fname == 'INTEGER.c':
                OLD_RANGE = (
                    '\t/* X.691-11/2008, #13.2.2, test if constrained whole number */\n'
                    '\tif(ct && ct->range_bits >= 0) {\n'
                    '        unsigned long v;\n'
                    '\t\t/* #11.5.6 -> #11.3 */\n'
                    '\t\tASN_DEBUG("Encoding integer %ld (%lu) with range %d bits",\n'
                    '\t\t\tvalue, value - ct->lower_bound, ct->range_bits);\n'
                    '        if(per_long_range_rebase(value, ct->lower_bound, ct->upper_bound, &v)) {\n'
                    '            ASN__ENCODE_FAILED;\n'
                    '        }\n'
                    '        if(uper_put_constrained_whole_number_u(po, v, ct->range_bits))\n'
                    '            ASN__ENCODE_FAILED;\n'
                    '\t\tASN__ENCODED_OK(er);\n'
                    '\t}'
                )
                NEW_RANGE = (
                    '\t/* X.691-11/2008, #13.2.2, test if constrained whole number */\n'
                    '\tif(ct && ct->range_bits >= 0) {\n'
                    '        unsigned long v;\n'
                    '\t\t/* #11.5.6 -> #11.3 */\n'
                    '\t\tASN_DEBUG("Encoding integer %ld (%lu) with range %d bits",\n'
                    '\t\t\tvalue, value - ct->lower_bound, ct->range_bits);\n'
                    '        if(specs && specs->field_unsigned) {\n'
                    '            /* Unsigned integer: use unsigned comparison to handle ULONG_MAX upper bounds */\n'
                    '            unsigned long uval = (unsigned long)value;\n'
                    '            if(uval < (unsigned long)ct->lower_bound || uval > (unsigned long)ct->upper_bound)\n'
                    '                ASN__ENCODE_FAILED;\n'
                    '            v = uval - (unsigned long)ct->lower_bound;\n'
                    '        } else {\n'
                    '            if(per_long_range_rebase(value, ct->lower_bound, ct->upper_bound, &v)) {\n'
                    '                ASN__ENCODE_FAILED;\n'
                    '            }\n'
                    '        }\n'
                    '        if(uper_put_constrained_whole_number_u(po, v, ct->range_bits))\n'
                    '            ASN__ENCODE_FAILED;\n'
                    '\t\tASN__ENCODED_OK(er);\n'
                    '\t}'
                )
                if OLD_RANGE in text and NEW_RANGE not in text:
                    text = text.replace(OLD_RANGE, NEW_RANGE)
                    with open(fpath, 'w', encoding='utf-8') as f:
                        f.write(text)
                    fixed_files += 1

    print(f"Исправлено файлов: {fixed_files}")


if __name__ == '__main__':
    run()

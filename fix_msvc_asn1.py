"""
Пост-обработка сгенерированных asn1c файлов для MSVC:
1. Пустые union { } choice; → добавляем char _msvc_dummy_;
2. Пустые struct { } ...;   → аналогично
"""
import os
import re

DIRS = [
    r"src\generated\s1ap",
    r"src\generated\x2ap",
    r"src\generated\nbap",
]

base = os.path.dirname(os.path.abspath(__file__))

# Паттерн: union SomeName_u {\n\t} choice;
# или:      union SomeName_u {\n} choice;
EMPTY_UNION = re.compile(
    r'(union\s+\w+\s*\{)\s*\n(\s*\})',
    re.MULTILINE
)
EMPTY_STRUCT = re.compile(
    r'(struct\s+\w+\s*\{)\s*\n(\s*\})',
    re.MULTILINE
)

fixed_files = 0

for d in DIRS:
    folder = os.path.join(base, d)
    for fname in os.listdir(folder):
        if not (fname.endswith('.h') or fname.endswith('.c')):
            continue
        fpath = os.path.join(folder, fname)
        with open(fpath, 'r', encoding='utf-8') as f:
            text = f.read()
        original = text

        # Вставляем char _msvc_dummy_; в пустые union
        def fix_empty_body(m):
            prefix = m.group(1)
            suffix = m.group(2)
            # Определяем отступ закрывающей скобки
            indent = re.match(r'\s*', suffix).group()
            return f"{prefix}\n{indent}\tchar _msvc_dummy_;\n{suffix}"

        text = EMPTY_UNION.sub(fix_empty_body, text)
        text = EMPTY_STRUCT.sub(fix_empty_body, text)

        # Add <inttypes.h> inside the _WIN32/_MSC_VER block if missing
        # (standard asn1c v0.9.29 omits it; asn1c-mouse adds it)
        MSVC_WIN32_BLOCK = '#include <windows.h>\n#include <float.h>\n'
        INTTYPES_LINE = '#include <inttypes.h>   /* PRId32, PRIu32, PRIdMAX, PRIuMAX, etc. */\n'
        if fname == 'asn_system.h' and MSVC_WIN32_BLOCK in text and INTTYPES_LINE not in text:
            text = text.replace(
                MSVC_WIN32_BLOCK,
                MSVC_WIN32_BLOCK + INTTYPES_LINE,
            )

        if text != original:
            with open(fpath, 'w', encoding='utf-8') as f:
                f.write(text)
            fixed_files += 1

print(f"Исправлено файлов: {fixed_files}")

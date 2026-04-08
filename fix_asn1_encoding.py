"""
Заменяет все не-ASCII символы в .asn файлах на их ASCII-эквивалент или пробел.
NBSP (U+00A0) -> пробел, em-dash/en-dash -> дефис, прочие -> пробел.
"""
import os
import re

DIRS = [
    r"asn1\s1ap",
    r"asn1\x2ap",
    r"asn1\nbap",
]

REPLACEMENTS = {
    '\u00a0': ' ',   # NBSP
    '\u2013': '-',   # en-dash
    '\u2014': '-',   # em-dash
    '\u2018': "'",   # left single quote
    '\u2019': "'",   # right single quote
    '\u201c': '"',   # left double quote
    '\u201d': '"',   # right double quote
    '\u2022': '-',   # bullet
    '\u00b7': '.',   # middle dot
}

base = os.path.dirname(os.path.abspath(__file__))

for d in DIRS:
    folder = os.path.join(base, d)
    for fname in os.listdir(folder):
        if not fname.endswith('.asn'):
            continue
        fpath = os.path.join(folder, fname)
        with open(fpath, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
        original = text
        for bad, good in REPLACEMENTS.items():
            text = text.replace(bad, good)
        # любые оставшиеся не-ASCII заменяем пробелом
        text = re.sub(r'[^\x00-\x7F]', ' ', text)
        if text != original:
            with open(fpath, 'w', encoding='utf-8') as f:
                f.write(text)
            print(f"Исправлен: {fname}")
        else:
            print(f"Чистый:    {fname}")

print("Готово.")

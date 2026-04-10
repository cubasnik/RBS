---
description: "Regenerate ASN.1 C sources for S1AP/X2AP/NBAP and apply all fix scripts. Use when: asn1 regeneration, gen_asn1.sh, fix_*.py, generated sources out of date, protocol IE added."
argument-hint: "Optional: s1ap | x2ap | nbap | all (default: all)"
---
Regenerate ASN.1 sources for the RBS project and apply all fix scripts.
Target protocol: ${input:target:all}

## Step 1 — Fix encoding in .asn spec files

Run `fix_asn1_encoding.py` to replace non-ASCII characters in the spec files before generation:

```bash
cd /c/Users/Alexey/Desktop/min/vNE/RBS/RBS
python fix_asn1_encoding.py
```

Confirm every `.asn` file prints "Чистый" or "Исправлен".

## Step 2 — Run asn1c code generator

Run `gen_asn1.sh` (or `gen_nbap.sh` for NBAP-only with timeout protection):

```bash
bash gen_asn1.sh
```

Check exit codes and file counts printed at the end:
- S1AP: expect ~90+ .c files
- X2AP: expect ~120+ .c files
- NBAP: expect 60+ .c files (may time out at 120 s — partial output is acceptable)

If only one protocol needs regeneration, delete only that subdirectory first:
```bash
rm -rf src/generated/s1ap   # or x2ap / nbap
```

## Step 3 — Apply fix scripts (run in order)

```bash
python fix_x2ap_c_includes.py
python fix_x2ap_cycles.py
python fix_x2ap_pdu_alias.py
python fix_protocol_ie_field.py
python fix_static_redefs.py
python fix_msvc_asn1.py
```

Each script prints what it changed. If a script reports errors, fix the script before continuing.

## Step 4 — Verify build

Open the solution in the `build-asn1/` directory and build targets `rbs_asn1_s1ap` and `rbs_asn1_x2ap`:

```bash
cd build-asn1
cmake --build . --config Debug --target rbs_asn1_s1ap rbs_asn1_x2ap
```

A clean build (0 errors) means the pipeline succeeded.
Warnings from generated code are expected — do not treat them as failures.

## Step 5 — Rebuild main project

```bash
cd ../build
cmake --build . --config Debug
```

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Duplicate symbol linker error | New IE type added → duplicate statics | Re-run `fix_protocol_ie_field.py` or `fix_static_redefs.py` |
| Circular include compile error | New container type | Re-run `fix_x2ap_cycles.py` |
| `aioc__undefined` missing | `asn_ioc.h` not patched | Check `fix_x2ap_cycles.py` patch for `asn_ioc.h` |
| `C2016` empty union | ProtocolExtensionField union empty | Check `fix_x2ap_cycles.py` empty-union patch |
| NBAP timed out, partial files | Large spec, normal | Use `gen_nbap.sh` (120 s timeout) |
| Non-ASCII in generated `.c` | Spec file had encoding issues | Re-run `fix_asn1_encoding.py`, then regenerate |

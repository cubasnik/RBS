#!/bin/bash
# NBAP с таймаутом 120 сек — генерируем что успеем
ulimit -s unlimited
PROJ='/c/Users/Alexey/Desktop/min/vNE/RBS/RBS'
ASN1C=/usr/local/bin/asn1c-mouse

echo "=== NBAP (timeout 120s) ==="
mkdir -p "$PROJ/src/generated/nbap"
cd "$PROJ/src/generated/nbap"
timeout 120 $ASN1C -fno-include-deps -no-gen-OER -no-gen-BER -no-gen-XER \
  -no-gen-JER -no-gen-CBOR -no-gen-UPER -fcompound-names -no-gen-example \
  -D . \
  "$PROJ/asn1/nbap/NBAP-CommonDataTypes.asn" \
  "$PROJ/asn1/nbap/NBAP-Constants.asn" \
  "$PROJ/asn1/nbap/NBAP-Containers.asn" \
  "$PROJ/asn1/nbap/NBAP-IEs.asn" \
  "$PROJ/asn1/nbap/NBAP-PDU-Contents.asn" \
  "$PROJ/asn1/nbap/NBAP-PDU-Descriptions.asn" 2>&1
echo "NBAP exit=$?  .c=$(ls *.c 2>/dev/null | wc -l)  .h=$(ls *.h 2>/dev/null | wc -l)"

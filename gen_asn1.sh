#!/bin/bash
ulimit -s unlimited
PROJ='/c/Users/Alexey/Desktop/min/vNE/RBS/RBS'
ASN1C=/usr/local/bin/asn1c-mouse

echo "=== S1AP ==="
rm -rf "$PROJ/src/generated/s1ap"
mkdir -p "$PROJ/src/generated/s1ap"
cd "$PROJ/src/generated/s1ap"
$ASN1C -no-gen-OER -no-gen-BER -no-gen-XER -no-gen-JER \
  -no-gen-CBOR -no-gen-UPER -fcompound-names -no-gen-example -pdu=auto -D . \
  "$PROJ/asn1/s1ap/S1AP-CommonDataTypes.asn" \
  "$PROJ/asn1/s1ap/S1AP-Constants.asn" \
  "$PROJ/asn1/s1ap/S1AP-Containers.asn" \
  "$PROJ/asn1/s1ap/S1AP-IEs.asn" \
  "$PROJ/asn1/s1ap/S1AP-PDU-Contents.asn" \
  "$PROJ/asn1/s1ap/S1AP-PDU-Descriptions.asn"
echo "S1AP exit=$?  .c=$(ls *.c 2>/dev/null | wc -l)  .h=$(ls *.h 2>/dev/null | wc -l)"

echo "=== X2AP ==="
rm -rf "$PROJ/src/generated/x2ap"
mkdir -p "$PROJ/src/generated/x2ap"
cd "$PROJ/src/generated/x2ap"
$ASN1C -no-gen-OER -no-gen-BER -no-gen-XER -no-gen-JER \
  -no-gen-CBOR -no-gen-UPER -fcompound-names -no-gen-example -pdu=auto -D . \
  "$PROJ/asn1/x2ap/X2AP-CommonDataTypes.asn" \
  "$PROJ/asn1/x2ap/X2AP-Constants.asn" \
  "$PROJ/asn1/x2ap/X2AP-Containers.asn" \
  "$PROJ/asn1/x2ap/X2AP-IEs.asn" \
  "$PROJ/asn1/x2ap/X2AP-PDU-Contents.asn" \
  "$PROJ/asn1/x2ap/X2AP-PDU-Descriptions.asn"
echo "X2AP exit=$?  .c=$(ls *.c 2>/dev/null | wc -l)  .h=$(ls *.h 2>/dev/null | wc -l)"

echo "=== NBAP ==="
mkdir -p "$PROJ/src/generated/nbap"
cd "$PROJ/src/generated/nbap"
$ASN1C -fno-include-deps -no-gen-OER -no-gen-BER -no-gen-XER -no-gen-JER \
  -no-gen-CBOR -no-gen-UPER -fcompound-names -no-gen-example -pdu=auto -D . \
  "$PROJ/asn1/nbap/NBAP-CommonDataTypes.asn" \
  "$PROJ/asn1/nbap/NBAP-Constants.asn" \
  "$PROJ/asn1/nbap/NBAP-Containers.asn" \
  "$PROJ/asn1/nbap/NBAP-IEs.asn" \
  "$PROJ/asn1/nbap/NBAP-PDU-Contents.asn" \
  "$PROJ/asn1/nbap/NBAP-PDU-Descriptions.asn"
echo "NBAP exit=$?  .c=$(ls *.c 2>/dev/null | wc -l)  .h=$(ls *.h 2>/dev/null | wc -l)"

echo "=== ИТОГО ==="
echo "S1AP: $(ls $PROJ/src/generated/s1ap/*.c 2>/dev/null | wc -l) .c файлов"
echo "X2AP: $(ls $PROJ/src/generated/x2ap/*.c 2>/dev/null | wc -l) .c файлов"
echo "NBAP: $(ls $PROJ/src/generated/nbap/*.c 2>/dev/null | wc -l) .c файлов"

echo "=== X2AP ==="
mkdir -p "$PROJ/src/generated/x2ap"
cd "$PROJ/src/generated/x2ap"
$ASN1C -pdu=auto -fno-include-deps -gen-PER -no-gen-OER -fcompound-names -no-gen-example -D . \
  "$PROJ/asn1/x2ap/X2AP-CommonDataTypes.asn" \
  "$PROJ/asn1/x2ap/X2AP-Constants.asn" \
  "$PROJ/asn1/x2ap/X2AP-Containers.asn" \
  "$PROJ/asn1/x2ap/X2AP-IEs.asn" \
  "$PROJ/asn1/x2ap/X2AP-PDU-Contents.asn" \
  "$PROJ/asn1/x2ap/X2AP-PDU-Descriptions.asn"
echo "X2AP .c: $(ls *.c 2>/dev/null | wc -l)  .h: $(ls *.h 2>/dev/null | wc -l)"

echo "=== NBAP ==="
mkdir -p "$PROJ/src/generated/nbap"
cd "$PROJ/src/generated/nbap"
$ASN1C -pdu=auto -fno-include-deps -gen-PER -no-gen-OER -fcompound-names -no-gen-example -D . \
  "$PROJ/asn1/nbap/NBAP-CommonDataTypes.asn" \
  "$PROJ/asn1/nbap/NBAP-Constants.asn" \
  "$PROJ/asn1/nbap/NBAP-Containers.asn" \
  "$PROJ/asn1/nbap/NBAP-IEs.asn" \
  "$PROJ/asn1/nbap/NBAP-PDU-Contents.asn" \
  "$PROJ/asn1/nbap/NBAP-PDU-Descriptions.asn"
echo "NBAP .c: $(ls *.c 2>/dev/null | wc -l)  .h: $(ls *.h 2>/dev/null | wc -l)"

echo "=== ГОТОВО ==="

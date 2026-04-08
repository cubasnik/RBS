#!/bin/bash
# Диагностика - передаём файлы по одному
ulimit -s unlimited
PROJ='/c/Users/Alexey/Desktop/min/vNE/RBS/RBS'
ASN1C=/usr/local/bin/asn1c
TMP=/tmp/asn1_test
mkdir -p $TMP

echo "--- Тест: только S1AP-CommonDataTypes ---"
cd $TMP && rm -f *.c *.h
$ASN1C -fno-include-deps -no-gen-OER -fcompound-names -no-gen-example -D . \
  "$PROJ/asn1/s1ap/S1AP-CommonDataTypes.asn" 2>&1; echo "exit=$?"

echo "--- Тест: только S1AP-Constants ---"
cd $TMP && rm -f *.c *.h
$ASN1C -fno-include-deps -no-gen-OER -fcompound-names -no-gen-example -D . \
  "$PROJ/asn1/s1ap/S1AP-Constants.asn" 2>&1; echo "exit=$?"

echo "--- Тест: CommonDataTypes + Constants + Containers ---"
cd $TMP && rm -f *.c *.h
$ASN1C -fno-include-deps -no-gen-OER -fcompound-names -no-gen-example -D . \
  "$PROJ/asn1/s1ap/S1AP-CommonDataTypes.asn" \
  "$PROJ/asn1/s1ap/S1AP-Constants.asn" \
  "$PROJ/asn1/s1ap/S1AP-Containers.asn" 2>&1; echo "exit=$?"

echo "--- Тест: + IEs ---"
cd $TMP && rm -f *.c *.h
$ASN1C -fno-include-deps -no-gen-OER -fcompound-names -no-gen-example -D . \
  "$PROJ/asn1/s1ap/S1AP-CommonDataTypes.asn" \
  "$PROJ/asn1/s1ap/S1AP-Constants.asn" \
  "$PROJ/asn1/s1ap/S1AP-Containers.asn" \
  "$PROJ/asn1/s1ap/S1AP-IEs.asn" 2>&1; echo "exit=$?"

echo "--- Тест: + PDU-Contents ---"
cd $TMP && rm -f *.c *.h
$ASN1C -fno-include-deps -no-gen-OER -fcompound-names -no-gen-example -D . \
  "$PROJ/asn1/s1ap/S1AP-CommonDataTypes.asn" \
  "$PROJ/asn1/s1ap/S1AP-Constants.asn" \
  "$PROJ/asn1/s1ap/S1AP-Containers.asn" \
  "$PROJ/asn1/s1ap/S1AP-IEs.asn" \
  "$PROJ/asn1/s1ap/S1AP-PDU-Contents.asn" 2>&1; echo "exit=$?"

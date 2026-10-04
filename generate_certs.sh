#!/bin/bash
set -e

mkdir -p certs
cd certs

openssl genrsa -out ca.key 4096
openssl req -x509 -new -nodes -key ca.key -sha256 -days 365 \
  -out ca.crt -subj "/C=BR/O=MiniSNMP/OU=Lab/CN=MiniSNMP-CA"

openssl genrsa -out agent.key 2048
openssl req -new -key agent.key -out agent.csr \
  -subj "/C=BR/O=MiniSNMP/OU=Agent/CN=localhost"
printf "subjectAltName=DNS:localhost,IP:127.0.0.1\nextendedKeyUsage=serverAuth\n" > agent.ext
openssl x509 -req -in agent.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out agent.crt -days 365 -sha256 -extfile agent.ext

openssl genrsa -out manager.key 2048
openssl req -new -key manager.key -out manager.csr \
  -subj "/C=BR/O=MiniSNMP/OU=Manager/CN=localhost"
printf "subjectAltName=DNS:localhost,IP:127.0.0.1\nextendedKeyUsage=clientAuth,serverAuth\n" > manager.ext
openssl x509 -req -in manager.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out manager.crt -days 365 -sha256 -extfile manager.ext

rm -f *.csr *.ext *.srl
chmod 600 *.key

echo "Certificados TLS criados em ./certs/"

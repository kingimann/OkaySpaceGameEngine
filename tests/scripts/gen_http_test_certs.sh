#!/bin/sh
# Makes the throw-away certificates for tests/test_own_http_net.cpp in <outdir>:
#   ca.pem            the CA the client is told to trust
#   good.crt/.key     valid, names: DNS localhost and IP 127.0.0.1, signed by ca
#   wronghost.*       valid and signed by ca, but only for DNS wrong.example
#   expired.*         signed by ca, right names, validity 2020-01-01 .. 2020-01-02
#   untrusted.*       right names, valid, signed by ca2 (a CA the client does not trust)
#   ca2.pem           the other CA
# Test keys only: they are created fresh on every run and never leave the temp directory.
set -eu
out=${1:?usage: gen_http_test_certs.sh <outdir>}
mkdir -p "$out"
cd "$out"
EC="-newkey ec -pkeyopt ec_paramgen_curve:prime256v1"

mkca() {
    n=$1
    mkdir -p "$n.db"; : > "$n.db/index.txt"; echo 01 > "$n.db/serial"
    cat > "$n.cnf" <<CNF
[ca]
default_ca = CA_default
[CA_default]
database = ./$n.db/index.txt
new_certs_dir = ./$n.db
serial = ./$n.db/serial
default_md = sha256
policy = policy_any
unique_subject = no
copy_extensions = none
certificate = ./$n.pem
private_key = ./$n.key
default_days = 3650
[policy_any]
commonName = supplied
CNF
    openssl req -x509 $EC -nodes -keyout "$n.key" -out "$n.pem" -days 3650 \
        -subj "/CN=Okay Test $n CA" -addext "basicConstraints=critical,CA:TRUE" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
}

# mkleaf <name> <san> <ca> [startdate enddate]
mkleaf() {
    n=$1; san=$2; ca=$3; shift 3
    openssl req $EC -nodes -keyout "$n.key" -out "$n.csr" -subj "/CN=$n" 2>/dev/null
    printf 'subjectAltName=%s\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=serverAuth\n' "$san" > "$n.ext"
    if [ $# -eq 2 ]; then
        openssl ca -batch -config "$ca.cnf" -in "$n.csr" -out "$n.crt" -extfile "$n.ext" -notext -startdate "$1" -enddate "$2" 2>/dev/null
    else
        openssl ca -batch -config "$ca.cnf" -in "$n.csr" -out "$n.crt" -extfile "$n.ext" -notext 2>/dev/null
    fi
}

mkca ca
mkca ca2
mkleaf good      "DNS:localhost,IP:127.0.0.1" ca
mkleaf wronghost "DNS:wrong.example"          ca
mkleaf expired   "DNS:localhost,IP:127.0.0.1" ca 20200101000000Z 20200102000000Z
mkleaf untrusted "DNS:localhost,IP:127.0.0.1" ca2
echo "certificates written to $out"

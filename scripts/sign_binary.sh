#!/usr/bin/env bash
# sign_binary.sh — Generate ECDSA P-256 keypair and sign the Aurore binary
# AM7-L2-SEC-002: Firmware images shall be signed with ECDSA P-256
#
# Usage:
#   ./scripts/sign_binary.sh genkey           -- generate keypair in /etc/aurore/
#   ./scripts/sign_binary.sh sign <binary>    -- sign a binary, write .sig file
#   ./scripts/sign_binary.sh verify <binary>  -- verify a binary against .sig
#   ./scripts/sign_binary.sh sign-self        -- sign the running aurore_main binary

set -euo pipefail

KEY_DIR="/etc/aurore"
PRIVKEY="${KEY_DIR}/signing_key.pem"
PUBKEY="${KEY_DIR}/signing_key.pub"
SIG_DIR="${KEY_DIR}"

die() { echo "ERROR: $*" >&2; exit 1; }

require_root() {
    [[ $EUID -eq 0 ]] || die "This command requires root (sudo)."
}

cmd_genkey() {
    require_root
    mkdir -p "${KEY_DIR}"
    chmod 700 "${KEY_DIR}"

    if [[ -f "${PRIVKEY}" ]]; then
        echo "Key already exists at ${PRIVKEY}. Remove it first to regenerate."
        exit 1
    fi

    echo "Generating ECDSA P-256 private key..."
    openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 \
        -out "${PRIVKEY}"
    chmod 600 "${PRIVKEY}"

    echo "Extracting public key..."
    openssl pkey -in "${PRIVKEY}" -pubout -out "${PUBKEY}"
    chmod 644 "${PUBKEY}"

    echo "Done."
    echo "  Private key : ${PRIVKEY} (mode 600, keep secret)"
    echo "  Public key  : ${PUBKEY}  (deploy alongside binary)"
}

cmd_sign() {
    local binary="${1:-}"
    [[ -n "${binary}" ]] || die "Usage: sign_binary.sh sign <binary>"
    [[ -f "${binary}" ]] || die "Binary not found: ${binary}"
    [[ -f "${PRIVKEY}" ]] || die "Private key not found at ${PRIVKEY} — run genkey first"

    local sig_path="${SIG_DIR}/$(basename "${binary}").sig"

    echo "Computing SHA-256 hash of ${binary}..."
    local hash
    hash=$(openssl dgst -sha256 -binary "${binary}" | xxd -p | tr -d '\n')
    echo "  Hash: ${hash}"

    echo "Signing hash with ECDSA P-256..."
    # Sign the raw binary digest and output DER-encoded signature
    openssl dgst -sha256 -sign "${PRIVKEY}" -out "${sig_path}" "${binary}"
    chmod 644 "${sig_path}"

    echo "Signature written to ${sig_path}"
}

cmd_verify() {
    local binary="${1:-}"
    [[ -n "${binary}" ]] || die "Usage: sign_binary.sh verify <binary>"
    [[ -f "${binary}" ]] || die "Binary not found: ${binary}"
    [[ -f "${PUBKEY}" ]] || die "Public key not found at ${PUBKEY}"

    local sig_path="${SIG_DIR}/$(basename "${binary}").sig"
    [[ -f "${sig_path}" ]] || die "Signature not found at ${sig_path}"

    echo "Verifying ${binary} against ${sig_path}..."
    if openssl dgst -sha256 -verify "${PUBKEY}" -signature "${sig_path}" "${binary}"; then
        echo "VERIFIED: binary is authentic."
    else
        echo "FAILED: signature verification failed!" >&2
        exit 2
    fi
}

cmd_sign_self() {
    local self
    self=$(readlink -f /proc/self/exe 2>/dev/null || realpath /proc/self/exe)
    # Find aurore_main in the build tree if running the script directly
    local aurore_bin
    aurore_bin=$(command -v aurore_main 2>/dev/null || find /home /opt /usr/local -name aurore_main -type f 2>/dev/null | head -1 || true)
    [[ -n "${aurore_bin}" ]] || die "aurore_main not found in PATH or filesystem"
    cmd_sign "${aurore_bin}"
    # Place sig where verify_self() expects it
    local sig_src="${SIG_DIR}/aurore_main.sig"
    local sig_dst="${SIG_DIR}/aurore.sig"
    if [[ "${sig_src}" != "${sig_dst}" ]] && [[ -f "${sig_src}" ]]; then
        cp "${sig_src}" "${sig_dst}"
        echo "Copied to ${sig_dst} (location expected by verify_self())"
    fi
}

# ---- dispatch ----------------------------------------------------------------
COMMAND="${1:-}"
shift || true

case "${COMMAND}" in
    genkey)     cmd_genkey ;;
    sign)       cmd_sign "$@" ;;
    verify)     cmd_verify "$@" ;;
    sign-self)  cmd_sign_self ;;
    *)
        echo "Usage: $0 {genkey|sign <binary>|verify <binary>|sign-self}"
        exit 1
        ;;
esac

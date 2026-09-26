#!/usr/bin/env bash
set -Eeuo pipefail
terminal=$(realpath "${1:-build/galaxy-terminal}")
tmp=$(mktemp -d)
server=''
cleanup() {
    if [[ -n "$server" ]]; then kill "$server" 2>/dev/null || true; wait "$server" 2>/dev/null || true; fi
    rm -rf "$tmp"
}
trap cleanup EXIT
report_error() {
    local status=$?
    echo "CLI regression failed at line $1 (status $status)" >&2
    for file in ready help version env cwd error server.log; do
        if [[ -f "$tmp/$file" ]]; then
            echo "CLI diagnostic: $file" >&2
            cat "$tmp/$file" >&2
            echo >&2
        fi
    done
    return "$status"
}
trap 'report_error "$LINENO"' ERR
wait_file() {
    for attempt in {1..100}; do
        if [[ -s "$1" ]]; then return; fi
        sleep 0.05
    done
    cat "$tmp/server.log"
    echo "Timed out waiting for $1" >&2
    exit 1
}
GALAXY_SERVER_ONLY=server "$terminal" -- /bin/sh -c 'echo ready > "$1"; exec sleep 30' sh "$tmp/ready" >"$tmp/server.log" 2>&1 &
server=$!
wait_file "$tmp/ready"
timeout 5s "$terminal" --help >"$tmp/help"
rg_check() { grep -q -- "$1" "$2"; }
rg_check --working-directory "$tmp/help"
kill -0 "$server"
timeout 5s "$terminal" --version >"$tmp/version"
rg_check 'Galaxy Terminal 0.2.0' "$tmp/version"
kill -0 "$server"
GALAXY_CLIENT_ONLY=client "$terminal" --new-tab -e /bin/sh -c \
    'printf "%s:%s:%s" "$GALAXY_CLIENT_ONLY" "${GALAXY_SERVER_ONLY-unset}" "$2" > "$1"' sh "$tmp/env" --help
wait_file "$tmp/env"
[[ $(cat "$tmp/env") == client:unset:--help ]]
mkdir "$tmp/directory"
(cd "$tmp/directory" && "$terminal" --new-tab --working-directory . -- /bin/sh -c 'pwd > "$1"' sh "$tmp/cwd")
wait_file "$tmp/cwd"
# PWD may retain a trailing /. or a symlink: compare the actual directory.
[[ $(cat "$tmp/cwd") -ef "$tmp/directory" ]]
if "$terminal" -e >"$tmp/error" 2>&1; then echo 'Missing -e command was accepted' >&2; exit 1; fi
if "$terminal" --profile does-not-exist >"$tmp/error" 2>&1; then echo 'Unknown profile was accepted' >&2; exit 1; fi
kill -0 "$server"
echo 'CLI forwarding, help/version, -e, environment and working directory: passed'

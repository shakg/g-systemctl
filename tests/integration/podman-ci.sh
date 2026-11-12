#!/usr/bin/env bash
# Runs the runtime integration tests and a TUI smoke test against podman.
# Used by .github/workflows/podman.yml; also runs locally (as root for rootful podman).
#
#   tests/integration/podman-ci.sh [BUILD_DIR]
#
# BUILD_DIR must be configured with -DBUILD_TESTING=ON (default: build).
# Env: GSYSTEMCTL_COMPOSE (e.g. podman-compose), GSYSTEMCTL_EXPECT_DRIFT (1/0),
#      GSYSTEMCTL_TEST_IMAGE. Needs podman, tmux and a compose provider.
set -euo pipefail

build=$(cd "${1:-build}" && pwd)
image=${GSYSTEMCTL_TEST_IMAGE:-docker.io/library/alpine:3.20}
export GSYSTEMCTL_INTEGRATION=1 GSYSTEMCTL_RUNTIME=podman GSYSTEMCTL_TEST_IMAGE=$image

podman version
podman info --format 'rootless={{.Host.Security.Rootless}} cgroups={{.Host.CgroupsVersion}} manager={{.Host.CgroupManager}}'

# `podman compose` runs docker-compose against podman's API socket.
sock=$(podman info --format '{{.Host.RemoteSocket.Path}}')
if [ ! -S "$sock" ]; then
    mkdir -p "$(dirname "$sock")"
    nohup podman system service --time=0 "unix://$sock" >/tmp/podman-service.log 2>&1 &
    for _ in $(seq 40); do [ -S "$sock" ] && break; sleep 0.25; done
fi

for i in 1 2 3; do podman pull -q "$image" && break; sleep $((i * 5)); done

echo "::group::integration tests"
"$build/tests/g-systemctl-tests" --gtest_filter='*Integration*'
echo "::endgroup::"

# The binary itself: lists a container in a pod, hides the pod's infra container.
session=gsc-smoke
cleanup() {
    tmux kill-session -t "$session" 2>/dev/null || true
    podman pod rm -f gsc-ui-pod >/dev/null 2>&1 || true
}
trap cleanup EXIT
cleanup
podman pod create --name gsc-ui-pod >/dev/null
podman run -d --pod gsc-ui-pod --name gsc-ui "$image" sleep 300 >/dev/null

tmux new-session -d -s "$session" -x 120 -y 30 "env GSYSTEMCTL_RUNTIME=podman $build/g-systemctl -c"
screen=""
for _ in $(seq 60); do
    screen=$(tmux capture-pane -p -t "$session")
    grep -q 'gsc-ui ' <<<"$screen" && break
    sleep 0.5
done
echo "$screen"
tmux send-keys -t "$session" M-q

grep -q 'gsc-ui ' <<<"$screen" || { echo "::error::gsc-ui is not listed in the TUI"; exit 1; }
if grep -q -- '-infra' <<<"$screen"; then
    echo "::error::the pod's infra container is listed"
    exit 1
fi
echo "TUI smoke test passed"

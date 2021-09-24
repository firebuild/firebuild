#!/usr/bin/env bats

load test_helper

setup() {
  rm -rf test_cache_dir
}

@test "shim" {
  for i in 1 2; do
    result=$(./run-firebuild -o 'intercepted_commads_dir = "./bin"' -r -- bash -c "(sleep 0.1& sleep 0.1 2>&1 & fakeroot sleep 0.1); wait; (ls integration.bats; sh -c 'ls integration.bats') | uniq -c")
    assert_streq "$result" "      2 integration.bats"
    assert_streq "$(strip_stderr stderr)" ""
  result=$(../src/shim/firebuild-shim 2> stderr || true)
  assert_streq "$(cat stderr)" "ERROR: FB_SOCKET is not set, maybe firebuild is not running?"
  done
}

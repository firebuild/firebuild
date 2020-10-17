#!/usr/bin/env bats

load test_helper

setup() {
  rm -rf test_cache_dir
}

@test "--help" {
  result=$(./run-firebuild --help)
  echo "$result" | grep -q "in case of failure"
}

@test "bash -c ls" {
  # FIXME run this twice in a row, which doesn't work yet, needs stdout replaying
  for i in 1; do
    result=$(./run-firebuild -- bash -c "ls integration.bats")
    assert_streq "$result" "integration.bats"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "debugging with trace markers and report generation" {
  # FIXME run this twice in a row, which doesn't work yet, needs stdout replaying
  for i in 1; do
    result=$(./run-firebuild -r -d all -- bash -c "ls integration.bats; bash -c ls | tee dirlist > /dev/null")
    assert_streq "$result" "integration.bats"
  done
}

@test "bash exec chain" {
  # FIXME run this twice in a row, which doesn't work yet, needs stdout replaying
  for i in 1; do
    result=$(./run-firebuild -- bash -c "exec bash -c exec\\ bash\\ -c\\ ls\\\\\ integration.bats")
    assert_streq "$result" "integration.bats"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "simple pipe" {
  # FIXME run this twice in a row, which doesn't work yet, needs stdout replaying
  for i in 1; do
    result=$(./run-firebuild -- bash -c 'seq 10000 | grep ^9')
    assert_streq "$result" "$(seq 10000 | grep ^9)"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "parallel make" {
  for i in 1 2; do
    # clean up previous run
    make -s -f test_parallel_make.Makefile clean
    result=$(./run-firebuild -- make -s -j8 -f test_parallel_make.Makefile)
    assert_streq "$result" "ok"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "parallel sleeps" {
  for i in 1 2; do
    # TODO (rbalint) firebuild needs to handle many parallel processes
    result=$(./run-firebuild -- bash -c 'for i in $(seq 2000); do sleep 1 & done;  wait $(jobs -p)')
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "system()" {
  # FIXME run this twice in a row, which doesn't work yet, needs stdout replaying
  for i in 1; do
    result=$(./run-firebuild -- ./test_system)
    assert_streq "$result" "ok"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "exec()" {
  # FIXME run this twice in a row, which doesn't work yet, needs stdout replaying
  for i in 1; do
    result=$(./run-firebuild -- ./test_exec)
    assert_streq "$result" "ok"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "closedir() inside an rm -r" {
  for i in 1 2; do
    result=$(./run-firebuild -- bash -c 'mkdir -p TeMp/FoO; rm -r TeMp')
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "file operations" {
  for i in 1 2; do
    result=$(./run-firebuild -- ./test_file_ops)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""

    # Due to the "again" parameter the 1st level cannot be shortcut in
    # the first iteration, but the 2nd level (./test_file_ops_2) can,
    # it should fetch the cached entries stored in the previous run.
    result=$(./run-firebuild -- ./test_file_ops again)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "waiting for a child" {
  for i in 1 2; do
    result=$(./run-firebuild -o 'processes.skip_cache -= "touch"' -- ./test_wait)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""

    # Due to the "again" parameter the outer process cannot be shortcut
    # in the first iteration, but the children can, they should fetch
    # the cached entries stored in the previous run.
    result=$(./run-firebuild -o 'processes.skip_cache -= "touch"' -- ./test_wait again)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""
  done
}

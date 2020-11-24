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
  for i in 1 2; do
    result=$(./run-firebuild -- bash -c "ls integration.bats")
    assert_streq "$result" "integration.bats"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "debugging with trace markers and report generation" {
  for i in 1 2; do
    result=$(./run-firebuild -r -d all -i -- bash -c "ls integration.bats; bash -c ls | tee dirlist > /dev/null")
    assert_streq "$result" "integration.bats"
  done
}

@test "bash exec chain" {
  for i in 1 2; do
    result=$(./run-firebuild -- bash -c "exec bash -c exec\\ bash\\ -c\\ ls\\\\\ integration.bats")
    assert_streq "$result" "integration.bats"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "simple pipe" {
  for i in 1 2; do
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

@test "runaway sleep" {
  for i in 1 2; do
    result=$(./run-firebuild -- bash -c 'for i in $(seq 10); do (sleep 0.1; ls integration.bats; false)& done; echo foo' | sort)
    assert_streq "$result" "$(echo 'foo'; for i in $(seq 10); do echo 'integration.bats'; done)"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "system()" {
  for i in 1 2; do
    result=$(./run-firebuild -- ./test_system)
    assert_streq "$result" "ok"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "exec()" {
  for i in 1 2; do
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
    # clean up before running the test
    rm -rf test_directory/
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

@test "err()" {
  stderr_expected=$'test_err: warn1: No such file or directory\ntest_err: warn2: Permission denied\ntest_err: err1: No such file or directory\natexit_handler'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_err || true)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" "$stderr_expected"
  done
}

@test "error()" {
  stderr_expected=$'./test_error: error1: No such file or directory\n./test_error: error2: Permission denied\n./test_error: error3: No such file or directory\natexit_handler'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_error || true)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" "$stderr_expected"
  done
}

@test "env fixup" {
  for i in 1 2; do
    result=$(./run-firebuild -- ./test_env_fixup)
    echo "$result" | grep -qx "AAA=aaa"
    echo "$result" | grep -qx "BBB=bbb"
    echo "$result" | grep -qx "LD_PRELOAD=LIBXXX.SO LIBYYY.SO libfbintercept.so"
    strip_stderr stderr | grep -q "ERROR: ld.so: object 'LIBXXX.SO' from LD_PRELOAD cannot be preloaded"
    strip_stderr stderr | grep -q "ERROR: ld.so: object 'LIBYYY.SO' from LD_PRELOAD cannot be preloaded"
  done
}

@test "fork() + exec() a statically linked binary" {
  ldd ./test_static 2>&1 | egrep -q '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_fork_exec ./test_static)
    assert_streq "$result" "I am statically linked."
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "popen() a statically linked binary" {
  ldd ./test_static 2>&1 | egrep -q '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_popen ./test_static)
    assert_streq "$result" "I am statically linked."
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "posix_spawn() a statically linked binary" {
  ldd ./test_static 2>&1 | egrep -q '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_posix_spawn ./test_static)
    assert_streq "$result" "I am statically linked."
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "system() a statically linked binary" {
  ldd ./test_static 2>&1 | egrep -q '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_system ./test_static)
    assert_streq "$result" "I am statically linked."
    assert_streq "$(strip_stderr stderr)" ""
  done
}

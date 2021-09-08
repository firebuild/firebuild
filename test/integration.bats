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

@test "bash -c grep ok" {
  for i in 1 2; do
    result=$(echo -e "foo\nok\nbar" | ./run-firebuild -- bash -c "grep ok")
    assert_streq "$result" "ok"
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
    result=$(./run-firebuild -- bash -c 'seq 30000 | (sleep 0.01 && grep ^9)')
    assert_streq "$result" "$(seq 10000 | grep ^9)"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "yes | head" {
  for i in 1 2; do
    result=$(./run-firebuild -- bash -c 'yes | head -n 10000000 | tail -n 1')
    assert_streq "$result" "y"
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
    # Valgrind ignores the limit bumped internally in firebuild
    # See: https://bugs.kde.org/show_bug.cgi?id=432508
    result=$(set | grep -q valgrind && ulimit -S -n 8000 ; ./run-firebuild -- bash -c 'for i in $(seq 2000); do sleep 1 & done;  wait $(jobs -p)')
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
    echo "$result" | grep -qx "LD_PRELOAD=  LIBXXX.SO  libfirebuild.so  LIBYYY.SO  "
    strip_stderr stderr | grep -q "ERROR: ld.so: object 'LIBXXX.SO' from LD_PRELOAD cannot be preloaded"
    strip_stderr stderr | grep -q "ERROR: ld.so: object 'LIBYYY.SO' from LD_PRELOAD cannot be preloaded"
    # Valgrind finds an error in fakeroot https://bugs.debian.org/983272
    if ! set | grep -q valgrind; then
      result=$(fakeroot ./run-firebuild -- id -u)
      assert_streq "$result" "0"
    fi
    result=$(./run-firebuild -- fakeroot id -u)
    assert_streq "$result" "0"
  done
}

@test "popen() a statically linked binary and a normal one" {
  ldd ./test_static 2>&1 | egrep -q '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_popen ./test_static r)
    assert_streq "$result" "I am statically linked."
    assert_streq "$(strip_stderr stderr)" ""
    result=$(./run-firebuild -- bash -c "echo -e 'bar\nfoo\nbar' | ./test_cmd_popen 'grep foo' w")
    assert_streq "$result" "foo"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "fork() + exec() a statically linked binary" {
  ldd ./test_static 2>&1 | egrep -q '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_fork_exec ./test_static)
    assert_streq "$result" "I am statically linked."
    assert_streq "$(strip_stderr stderr)" ""
  done

  # command substitution with statically linked binary
  for i in 1 2; do
    result=$(timeout 10 ./run-firebuild -- sh -c 'echo `./test_static`')
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

@test "pipe replaying" {
  result=$(./run-firebuild -o 'processes.skip_cache -= "echo"' -o 'min_cpu_time = -1.0' -- echo foo)
  assert_streq "$result" "foo"
  assert_streq "$(strip_stderr stderr)" ""

  # Poison the cache, pretending that the output was "quux" instead of "foo".
  # (Bash supports wildcard redirection to a single matching file.)
  echo quux > test_cache_dir/blobs/*/*/*

  # Replaying the "echo foo" command now needs to print "quux".
  result=$(./run-firebuild -o 'processes.skip_cache -= "echo"' -- echo foo)
  assert_streq "$result" "quux"
  assert_streq "$(strip_stderr stderr)" ""
}

@test "shim" {
  for i in 1 2; do
    result=$(./run-firebuild -o 'use_shim = true' -o 'intercepted_commands_dir = "./bin"' -r -- bash -c "(sleep 0.1& sleep 0.1 2>&1 & fakeroot sleep 0.1); wait; (bin/ls integration.bats; sh -c 'ls integration.bats') | uniq -c")
    assert_streq "$result" "      2 integration.bats"
    assert_streq "$(strip_stderr stderr)" ""
    result=$(./run-firebuild -d proc -o 'use_shim = true' -o 'intercepted_commands_dir = "./bin"' -- ls Makefile)
    assert_streq "$result" "Makefile"
  done
  result=$(../src/shim/firebuild-shim 2> stderr || true)
  assert_streq "$(cat stderr)" "ERROR: FB_SOCKET is not set, maybe firebuild is not running?"
}

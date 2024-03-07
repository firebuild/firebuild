#!/usr/bin/env bats

load test_helper

setup() {
  rm -rf test_cache_dir
}

@test "--help, --version" {
  result=$(./run-firebuild --help)
  echo "$result" | grep -q "in case of failure"
  result=$(./run-firebuild --version)
  echo "$result" | grep -q "Firebuild Inc."
  assert_streq "$(strip_stderr stderr)" ""
}

@test "empty-config" {
  result=$(./run-firebuild -c empty-config.conf -- ls integration.bats)
    assert_streq "$result" "integration.bats"
    assert_streq "$(strip_stderr stderr)" ""
}

@test "bash -c ls" {
  for i in 1 2; do
    result=$(./run-firebuild -D foo -o 'processes.dont_shortcut -= "ls"'  -- bash -c "ls integration.bats")
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
    result=$(./run-firebuild -r -q -- bash -c "echo ok")
    assert_streq "$result" "ok"
    assert_streq "$(strip_stderr stderr)" ""

    # clean up before running the test
    rm -rf test_directory/ foo-dir/
    result=$(./run-firebuild -o 'processes.dont_shortcut -= "ls"' -C . --generate-report=firebuild-build-report.html -d all -i -- bash -c "ls integration.bats; bash -c ls | tee dirlist > /dev/null && ./test_file_ops")
    assert_streq "$result" "$(printf 'integration.bats\nFIREBUILD: Generated report: firebuild-build-report.html')"
  done
}

@test "bash exec chain" {
  for i in 1 2; do
    result=$(./run-firebuild -q -o 'processes.skip_cache -= "head"' -- bash -c "exec bash -c exec\\ bash\\ -c\\ head\\\\\ -n1\\\\\ integration.bats")
    assert_streq "$result" "#!/usr/bin/env bats"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "dash bash exec chain with allow-list" {
  for i in 1 2; do
    result=$(./run-firebuild -d cache -o 'processes.skip_cache -= "head"' -o 'processes.shortcut_allow_list += "bash"' -- dash -c "exec bash -c exec\\ bash\\ -c\\ head\\\\\ -n1\\\\\ integration.bats")
    assert_streq "$result" "#!/usr/bin/env bats"
    assert_streq "$(strip_stderr stderr)" ""
    assert_streq "$(grep -h 'original_executed_path' test_cache_dir/objs/*/*/*/%_directory_debug.json | sed 's|/.*/||' | uniq -c | sed 's/  */ /g')" ' 2 "original_executed_path": "bash",'
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
    result=$(./run-firebuild -- bash -c 'yes 2> /dev/null | head -n 10000000 | tail -n 1')
    assert_streq "$result" "y"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "parallel make" {
  for i in 1 2; do
    # clean up previous run
    make -s -f test_parallel_make.Makefile clean
    result=$(./run-firebuild -d proc -o 'processes.jobserver_users += "make"' -- make -s -j8 -f test_parallel_make.Makefile)
    assert_streq "$result" "ok"
    assert_streq "$(strip_stderr stderr | grep 'Command ' | grep -E -v '(set to be not shortcut|matches skip_cache|Shortcut parent .* instead)')" ""
    result=$(./run-firebuild -s)
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "orphan processes" {
  # Orphan process detection does not work in WSL1
  [ "$(uname)" = "Linux" ] && [ "$(systemd-detect-virt)" != "wsl" ] || skip
  for i in 1 2; do
    if ! with_valgrind; then
      result=$(./run-firebuild -o 'processes.dont_shortcut += "sleep"' -- bash -c 'for i in $(seq 10); do (sleep 0.3; ls integration.bats; false)& done; /bin/echo foo' | sort)
    else
      result=$(./run-firebuild -o 'processes.dont_shortcut += "sleep"' -- bash -c 'for i in $(seq 10); do (sleep 1; ls integration.bats; false)& done; /bin/echo foo' | sort)
    fi
    assert_streq "$result" "foo"
    if [ "$(uname)" = "Linux" ]; then
      assert_streq "$(strip_stderr stderr | uniq -c)" "     10 Orphan process has been killed by signal 15"
    else
      assert_streq "$(strip_stderr stderr)" ""
    fi

    result=$(./run-firebuild -- ./test_orphan)
    assert_streq "$result" ""
    if [ "$(uname)" = "Linux" ]; then
      # there may be one or two detected orphan processes
      assert_streq "$(strip_stderr stderr | uniq)" "Orphan process has been killed by signal 15"
    else
      assert_streq "$(strip_stderr stderr)" ""
    fi
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
    rm -rf test_directory/ foo-dir/
    result=$(./run-firebuild -- ./test_file_ops)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""

    # Due to the "again" parameter the 1st level cannot be shortcut in
    # the first iteration, but the 2nd level (./test_file_ops_2) can,
    # it should fetch the cached entries stored in the previous run.
    result=$(./run-firebuild -- ./test_file_ops again)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""

    # test sendfile and friends
    result=$(./run-firebuild -- bash -c './test_sendfile < integration.bats > foo')
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""
    rm -f foo

    # The process can find a directory missing then can create a file in it due to the directory
    # having been created by an other parallel process
    result=$(./run-firebuild bash -c "(bash -c 'sh -c \"echo -x > foo-dir/bar1\" 2> /dev/null; sleep 0.2; sh -c \"echo x > foo-dir/bar2\" 2> /dev/null') & (sleep 0.1; [ $i == 2 ] || mkdir foo-dir); wait")
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""

    # Existing directories are not created again
    rm -rf foo-dir; mkdir foo-dir
    result=$(./run-firebuild bash -c "rm -rf foo-dir ; mkdir foo-dir")
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""

    # Two processes write a file in parallel
    # 1. One  executed process writer is an ancestor of the other.
    result=$(./run-firebuild -- bash -c '( (sleep 0.1; echo 1) >> foo)& sh -c "(sleep 0.11; echo 1) >> foo" ; wait')
    # 2. The writers have a common ancestor.
    result=$(./run-firebuild -- bash -c 'for i in 1 2; do sh -c "(sleep 0.1$i; echo 1) >> foo" & done; wait')
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""
    rm -f foo
  done
}

@test "randomness handling" {
  for i in 1 2; do
    result=$(./run-firebuild -o 'ignore_locations -= "/dev/urandom"' -- ./test_random)
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""
    result=$(./run-firebuild -- ./test_random again)
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

@test "pthreads interception" {
  for i in 1 2; do
    result=$(./run-firebuild -i -- ./test_pthreads)
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
  [ -x ./test_error ] || skip
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
    echo "$result" | grep -qx "BBB=bbb"
    if [ "$(uname)" = "Linux" ]; then
      echo "$result" | grep -qx "LD_PRELOAD=  LIBXXX.SO  libfirebuild.so  LIBYYY.SO  "
      strip_stderr stderr | grep -q "ERROR: ld.so: object 'LIBXXX.SO' from LD_PRELOAD cannot be preloaded"
      strip_stderr stderr | grep -q "ERROR: ld.so: object 'LIBYYY.SO' from LD_PRELOAD cannot be preloaded"
    else
      echo "$result" | grep -qx "DYLD_INSERT_LIBRARIES=libfirebuild.dylib"
    fi
    # Valgrind finds an error in fakeroot https://bugs.debian.org/983272
    if fakeroot ls > /dev/null 2>&1; then
      if ! with_valgrind; then
        result=$(fakeroot ./run-firebuild -- id -u)
        assert_streq "$result" "0"
        result=$(./run-firebuild -- fakeroot id -u)
        assert_streq "$result" "0"
      fi
      result=$(./run-firebuild -- fakeroot id -u)
      assert_streq "$result" "0"
    fi
  done
}

@test "popen() a statically linked binary and a normal one" {
  [ -x ./test_static ] || skip
  ldd ./test_static 2>&1 | grep -Eq '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    if [ -x ./test_static ]; then
      result=$(./run-firebuild -- ./test_cmd_popen ./test_static r)
      assert_streq "$result" "I am statically linked."
      assert_streq "$(strip_stderr stderr)" ""
    fi
    result=$(./run-firebuild -- bash -c "echo -e 'bar\nfoo\nbar' | ./test_cmd_popen 'grep foo' w")
    assert_streq "$result" "foo"
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "fork() + exec() a statically linked binary" {
  [ -x ./test_static ] || skip
  ldd ./test_static 2>&1 | grep -Eq '(not a dynamic executable|statically linked)'

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

@test "posix_spawn() a binary" {
  for i in 1 2; do
    result=$(./run-firebuild -r -q -- ./test_cmd_posix_spawn bash -c 'echo ok')
    assert_streq "$result" "ok"
    assert_streq "$(strip_stderr stderr)" ""
  done
  result=$(./run-firebuild -s | grep Hits)
  assert_streq "$result" "  Hits:             1 / 3 (33.33 %)"
  assert_streq "$(strip_stderr stderr)" ""
}

@test "posix_spawn() a static binary" {
  [ -x ./test_static ] || skip
  ldd ./test_static 2>&1 | grep -Eq '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_posix_spawn ./test_static)
    assert_streq "$result" "I am statically linked."
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "clone() a statically linked binary" {
  [ -x ./test_cmd_clone ] || skip
  ldd ./test_static 2>&1 | grep -Eq '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_clone ./test_static | uniq -c)
    assert_streq "$result" "      2 I am statically linked."
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "system() a statically linked binary" {
  [ -x ./test_static ] || skip
  ldd ./test_static 2>&1 | grep -Eq '(not a dynamic executable|statically linked)'

  for i in 1 2; do
    result=$(./run-firebuild -- ./test_cmd_system './test_static 3' | tr '\n' ' ')
    assert_streq "$result" "I am statically linked. end "
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

@test "parallel sleeps" {
  for i in 1 2; do
    # Valgrind ignores the limit bumped internally in firebuild
    # See: https://bugs.kde.org/show_bug.cgi?id=432508
    result=$(with_valgrind && ulimit -S -n 8000 ; ./run-firebuild -- bash -c 'for i in $(if [ "$(uname)" = "Darwin" ]; then seq 400; else seq 2000; fi); do sleep 1 & done;  wait')
    assert_streq "$result" ""
    assert_streq "$(strip_stderr stderr)" ""
  done
}

@test "max entry size" {
  result=$(./run-firebuild -d caching -o 'processes.skip_cache -= "dd"' dd if=/dev/zero of=foo bs=1M count=251)
  assert_streq "$result" ""
  assert_streq "$(strip_stderr stderr | grep max_entry)" "FIREBUILD: Could not store blob in cache because it would exceed max_entry_size"
  result=$(./run-firebuild -d caching -o 'processes.skip_cache -= "dd"' -o 'max_entry_size = 0.0042' dd if=/dev/zero of=foo bs=4k count=1)
  assert_streq "$result" ""
  assert_streq "$(strip_stderr stderr | grep max_entry)" "FIREBUILD: Could not store entry in cache because it would exceed max_entry_size"
  rm -f foo
}

@test "gc" {
  rm -f foo
  result=$(./run-firebuild -d cache -- bash -c 'echo foo > foo')
  assert_streq "$result" ""
  if [ "$SKIP_GC_INVALID_ENTRIES_TEST" != 1 ]; then
    echo foo > test_cache_dir/blobs/invalid_blob_name
    echo bar > test_cache_dir/objs/invalid_obj_name
    ln -s invalid_blob_name test_cache_dir/blobs/unexpected_symlink
    ln -s invalid_obj_name test_cache_dir/objs/unexpected_symlink
  fi
  mkdir test_cache_dir/blobs/to_be_removed test_cache_dir/objs/to_be_removed
  touch test_cache_dir/objs/to_be_removed/%_directory_debug.json
  mkdir test_cache_dir/objs/many-entries
  for i in $(seq -w 30); do
    cp test_cache_dir/objs/?/??/*/??????????? test_cache_dir/objs/many-entries/12345678${i}+
  done
  # update cache size
  if [ "$(uname)" = "Linux" ]; then
    du="du --apparent-size -b"
  else
    du="stat -f %z"
  fi
  new_cache_size=$((cat test_cache_dir/size | tr '\n' ' ' ; printf '+ 30 *' ; ($du test_cache_dir/objs/?/??/*/??????????? | cut -f1) ) | bc)
  echo $new_cache_size > test_cache_dir/size

  result=$(./run-firebuild -o 'shortcut_tries = 18' -d cache --gc)
  assert_streq "$result" ""
  if [ "$SKIP_GC_INVALID_ENTRIES_TEST" != 1 ]; then
    assert_streq "$(grep 'invalid_.*_name' stderr | wc -l | sed 's/ *//g')" "2"
    assert_streq "$(strip_stderr stderr | grep -v 'invalid_.*_name' | grep -v 'type is unexpected')" "FIREBUILD ERROR: There are 8 bytes in the cache stored in files with unexpected name."
  fi
  # debug files are kept with "-d cache"
  [ -f test_cache_dir/objs/*/*/*/%_directory_debug.json ]
  # there is a non-directory debug json file as well
  debug_json=$(ls test_cache_dir/objs/*/*/*/*_debug.json | grep -v %_directory)
  [ -f "${debug_json}" ]
  [ -f test_cache_dir/blobs/*/*/*_debug.txt ]
  # empty dirs were removed from blobs/, the one in objs/ is kept due to %_directory_debug.json
  result=$(find test_cache_dir/blobs -name 'to_be_removed')
  assert_streq "$result" ""
  assert_streq "$(ls test_cache_dir/objs/many-entries/ | wc -l | sed 's/ *//g')" "18"

  result=$( ./run-firebuild --gc)
  assert_streq "$result" ""
  if [ "$SKIP_GC_INVALID_ENTRIES_TEST" != 1 ]; then
    assert_streq "$(grep 'invalid_.*_name' stderr | wc -l | sed 's/ *//g')" "2"
  fi
  assert_streq "$(strip_stderr stderr | grep -v 'invalid_.*_name' | grep -v ' unexpected')" ""
  # debug files are deleted without "-d cache"
  result=$(find test_cache_dir/ -name '*debug*')
  assert_streq "$result" ""
  result=$(find test_cache_dir/ -name 'to_be_removed')
  assert_streq "$result" ""

  result=$( ./run-firebuild -o 'max_cache_size = 0.00002' --gc)
  cp -r test_cache_dir test_cache_dir.bak
  assert_streq "$result" ""
  assert_streq "$(strip_stderr stderr | grep -v 'invalid_.*_name' | grep -v ' unexpected')" ""
  rm -f foo
}

@test "cache-format" {
  result=$(./run-firebuild -d cache -- bash -c 'echo foo > foo')
  assert_streq "$result" ""
  assert_streq "$(strip_stderr stderr)" ""
  assert_streq "$(cat test_cache_dir/cache-format)" "1"

  # older cache versions are OK (assuming they are handled)
  echo 0 > test_cache_dir/cache-format
  result=$(./run-firebuild -d cache -- bash -c 'echo foo > foo')
  assert_streq "$result" ""
  assert_streq "$(strip_stderr stderr)" ""
  assert_streq "$(cat test_cache_dir/cache-format)" "0"

  # future cache versions prevent using the cache
  echo 2 > test_cache_dir/cache-format
  result=$(./run-firebuild -d cache -- bash -c 'echo foo > foo')
  assert_streq "$result" ""
  assert_streq "$(strip_stderr stderr)" "FIREBUILD ERROR: Cache format version is not supported, not reading or writing the cache"

  # unknown cache versions prevent using the cache, too
  echo foo > test_cache_dir/cache-format
  result=$(./run-firebuild -d cache -- bash -c 'echo foo > foo')
  assert_streq "$result" ""
  assert_streq "$(strip_stderr stderr)" "FIREBUILD ERROR: Cache format version is not supported, not reading or writing the cache"
}

@test "stats" {
  # Populate the cache
  result=$(./run-firebuild -z -- bash -c 'ls integration.bats')
  result=$(./run-firebuild -o 'processes.skip_cache = []' -- bash -c 'head -n1 integration.bats')
  # Use stats for current run
  result=$(./run-firebuild -s bash -c 'head -n1 integration.bats' | sed 's/  */ /g;s/seconds/ms/;s/[0-9-][0-9\.]* ms/N ms/;s/[0-9-][0-9\.]* kB/N kB/')
  assert_streq "$result" "$(printf '#!/usr/bin/env bats\n\nStatistics of current run:\n Hits: 1 / 1 (100.00 %%)\n Misses: 0\n Uncacheable: 0\n GC runs: 0\nNewly cached: N kB\nSaved CPU time: N ms\n')"
  # use --show-stats together with --gc ...
  result=$(./run-firebuild --gc --show-stats | sed 's/  */ /g;s/seconds/ms/;s/[0-9-][0-9\.]* ms/N ms/;s/[0-9-][0-9\.]* kB/N kB/')
  assert_streq "$result" "$(printf 'Statistics of stored cache:\n Hits: 1 / 4 (25.00 %%)\n Misses: 3\n Uncacheable: 1\n GC runs: 1\nCache size: N kB\nSaved CPU time: N ms\n')"
  # ... and without --gc
  result=$(./run-firebuild -s | sed 's/  */ /g;s/seconds/ms/;s/[0-9-][0-9\.]* ms/N ms/;s/[0-9-][0-9\.]* kB/N kB/')
  assert_streq "$result" "$(printf 'Statistics of stored cache:\n Hits: 1 / 4 (25.00 %%)\n Misses: 3\n Uncacheable: 1\n GC runs: 1\nCache size: N kB\nSaved CPU time: N ms\n')"
  result=$(./run-firebuild -z)
  assert_streq "$result" ""
  assert_streq "$(strip_stderr stderr)" ""
  result=$(./run-firebuild -s | sed 's/  */ /g;s/seconds/ms/;s/[0-9-][0-9\.]* ms/N ms/;s/[0-9-][0-9\.]* kB/N kB/')
  assert_streq "$result" "$(printf 'Statistics of stored cache:\n Hits: 0 / 0 (0.00 %%)\n Misses: 0\n Uncacheable: 0\n GC runs: 0\nCache size: N kB\nSaved CPU time: N ms\n')"
}

@test "clang pch" {
  # this test is very slow under valgrind
  ! with_valgrind || skip
  for no_pch_param in "" "-fno-pch-timestamp"; do
    for i in 1 2; do
      rm -f test_pch.h.pch test_pch.*.s
      result=$(./run-firebuild -- clang -cc1 ${no_pch_param} $TEST_SOURCE_DIR/test_pch.h -emit-pch -o test_pch.h.pch)
      assert_streq "$result" ""
      assert_streq "$(strip_stderr stderr)" ""
      # not reproducible to check test_pch.h.pch's embedded timestamp again
      result=$(./run-firebuild -- clang -cc1 -include-pch test_pch.h.pch $TEST_SOURCE_DIR/test_pch.c -o test_pch.$i.s)
      assert_streq "$result" ""
      assert_streq "$(strip_stderr stderr)" ""
      sleep 0.01
      touch test_pch.h
      rm -f test_pch.h.pch test_pch.*.s
    done
  done
}

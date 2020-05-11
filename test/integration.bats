#!/usr/bin/env bats

load test_helper

@test "--help" {
      result=$(./run-firebuild --help)
      echo "$result" | grep -q "in case of failure"
}

@test "bash -c ls" {
      result=$(./run-firebuild -- bash -c "ls integration.bats" 2> stderr)
      [ "$result" = "integration.bats" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "debugging with trace markers and report generation" {
      result=$(./run-firebuild -r -d all -- bash -c "ls integration.bats; bash -c ls | tee dirlist > /dev/null")
      [ "$result" = "integration.bats" ]
}

@test "bash exec chain" {
      result=$(./run-firebuild -- bash -c "exec bash -c exec\\ bash\\ -c\\ ls\\\\\ integration.bats" 2> stderr)
      [ "$result" = "integration.bats" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "simple pipe" {
      result=$(./run-firebuild -- bash -c 'seq 10000 | grep ^9' 2> stderr)
      [ "$result" = "$(seq 10000 | grep ^9)" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "parallel make" {
      # clean up previous run
      make -s -f test_parallel_make.Makefile clean
      result=$(./run-firebuild -- make -s -j8 -f test_parallel_make.Makefile 2> stderr)
      [ "$result" = "ok" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "parallel sleeps" {
      # TODO (rbalint) firebuild needs to gracefully handle when it is out of file descriptors
      result=$(./run-firebuild -- bash -c 'for i in $(seq 200); do sleep 2 & done;  wait $(jobs -p)' 2>stderr)
      [ "$result" = "" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "system()" {
      result=$(./run-firebuild -- ./test_system 2> stderr)
      [ "$result" = "ok" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "exec()" {
      result=$(./run-firebuild -- ./test_exec 2> stderr)
      [ "$result" = "ok" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "closedir() inside an rm -r" {
      result=$(./run-firebuild -- bash -c 'mkdir -p TeMp/FoF; rm -r TeMp' 2> stderr)
      [ "$result" = "" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "file operations" {
      result=$(./run-firebuild -- ./test_file_ops 2> stderr)
      [ "$result" = "" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

@test "file operations again" {
      # Due to the "again" parameter the 1st level cannot be shortcut,
      # but the 2nd level (./test_file_ops_2) can, it should fetch the
      # cached entries stored by the previous test.
      result=$(./run-firebuild -- ./test_file_ops again 2> stderr)
      [ "$result" = "" ]
      strip_stderr stderr
      [ -z "$(strip_stderr stderr)" ]
}

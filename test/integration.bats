#!/usr/bin/env bats

@test "--help" {
      result=$(./run-firebuild --help)
      echo "$result" | grep -q "in case of failure"
}

@test "bash -c ls" {
      result=$(./run-firebuild -- bash -c "ls integration.bats")
      [ "$result" = "integration.bats" ]
}

@test "debugging with trace markers and report generation" {
      result=$(./run-firebuild -r -d 4 -- bash -c "ls integration.bats; bash -c ls | tee dirlist > /dev/null")
      [ "$result" = "integration.bats" ]
}

@test "bash exec chain" {
      result=$(./run-firebuild -- bash -c exec\ bash\ -c\ exec\\\ bash\\\ -c\\\ ls\\\\\\\ integration.bats)
      [ "$result" = "integration.bats" ]
}

@test "simple pipe" {
      result=$(./run-firebuild -- bash -c 'seq 10000 | grep ^9')
      [ "$result" = "$(seq 10000 | grep ^9)" ]
}

@test "2k parallel echo-s" {
      result="$(./run-firebuild -- parallel -j 2000 echo -- $(seq 1 2000))"
      [ "$(echo "$result" | sort)" = "$(seq 1 2000 | sort)" ]
}

@test "system()" {
      result=$(./run-firebuild -- ./test_system)
      [ "$result" = "ok" ]
}

@test "exec()" {
      result=$(./run-firebuild -- ./test_exec)
      [ "$result" = "ok" ]
}

check: parallel_make_test_main
	./$<

parallel_make_test.%.c:
	printf "void _"$(subst .,_,$@)"(){}" > $@

parallel_make_test_main.c:
	printf "#include <stdio.h>\nint main() {printf(\"ok\\\n\"); return 0;}" > $@

# Note the '+' prefix before the commad. This makes make keep the jobserver pipe open even when not invoking a sub-make.
# See: https://savannah.gnu.org/bugs/?47392
parallel_make_test_main: parallel_make_test_main.c $(patsubst %,parallel_make_test.%.o,$(shell seq 8))
	+gcc -flto=auto -o $@ $^

clean:
	rm -f parallel_make_test.*.? parallel_make_test_main*

.PHONY: clean

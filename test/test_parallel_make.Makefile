
all: $(patsubst %,parallel_make_test.%.txt,$(shell seq 8))
	@echo ok

parallel_make_test.%.txt:
	touch $@

clean:
	rm -f parallel_make_test.*.txt

.PHONY: clean

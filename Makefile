.PHONY: all

EXE = test_atomic
LIB = atomic_file.c

all: $(EXE)


%: %.c $(LIB)
	gcc $< $(LIB) -o $@ -std=gnu99 -Wall -Werror -pthread

clean:
	rm -rf $(EXE)

valgrind: $(EXE)
	valgrind --leak-check=yes ./test_atomic

test: valgrind
	@echo $(ipcs -m | grep $USER)



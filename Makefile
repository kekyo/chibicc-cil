.SUFFIXES:

AS=chibias
#AS=../chibias-cil/chibias/bin/Debug/net6.0/chibias

CFLAGS=-std=c11 -g -fno-common -Wall -Wno-switch

LIBC=../libc-cil/libc-bootstrap/bin/Debug/netstandard2.0/libc-bootstrap.dll

SRCS=$(wildcard *.c)
TEST_SRCS=$(wildcard test/*.c)

# Stage 1

OBJS1=$(SRCS:%.c=%.o)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $*.c

chibicc: $(OBJS1)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJS1): $(SRCS) chibicc.h

TESTS1=$(TEST_SRCS:.c=)

test/libc-bootstrap.dll: $(LIBC)
	cp $(LIBC) -t test

test/common.o: test/common test/test.h chibicc
	./chibicc -Iinclude -Itest -c -o $@ - < test/common

test/%: test/%.c chibicc test/common.o test/libc-bootstrap.dll
	./chibicc -Iinclude -Itest -c -o test/$*.o test/$*.c
	./chibicc -pthread -o $@ test/$*.o test/common.o

$(TESTS1): $(TEST_SRCS) test/test.h test/common.o ./chibicc test/libc-bootstrap.dll

test: $(TESTS1)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./chibicc

test-all: test test-stage2 test-stage3

# Stage 2

OBJS2=$(SRCS:%.c=stage2/%.o)

stage2/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage2
	cp $(LIBC) -t stage2

stage2/%.o: %.c
	mkdir -p stage2
	./chibicc -Iinclude -Itest -c -o $@ $<

stage2/chibicc: $(OBJS2)
	./chibicc -o $@ $^

$(OBJS2): $(SRCS) chibicc.h chibicc stage2/libc-bootstrap.dll

TESTS2=$(TEST_SRCS:%.c=stage2/%)

stage2/test/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage2/test
	cp $(LIBC) -t stage2/test

stage2/test/common.o: test/common test/test.h stage2/chibicc
	mkdir -p stage2/test
	./stage2/chibicc -Iinclude -Itest -c -o $@ - < test/common

stage2/test/%: test/%.c stage2/chibicc
	./stage2/chibicc -Iinclude -Itest -c -o stage2/test/$*.o test/$*.c
	./stage2/chibicc -pthread -o $@ stage2/test/$*.o stage2/test/common.o

$(TESTS2): $(TEST_SRCS) test/test.h stage2/test/common.o stage2/chibicc stage2/test/libc-bootstrap.dll

test-stage2: $(TESTS2)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./stage2/chibicc

# Stage 3

OBJS3=$(SRCS:%.c=stage3/%.o)

stage3/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage3
	cp $(LIBC) -t stage3

stage3/%.o: %.c
	mkdir -p stage3
	./stage2/chibicc -Iinclude -Itest -c -o $@ $<

stage3/chibicc: $(OBJS3)
	./stage2/chibicc -o $@ $^

$(OBJS3): $(SRCS) chibicc.h stage2/chibicc stage3/libc-bootstrap.dll

TESTS3=$(TEST_SRCS:%.c=stage3/%)

stage3/test/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage3/test
	cp $(LIBC) -t stage3/test

stage3/test/common.o: test/common test/test.h stage3/chibicc
	mkdir -p stage3/test
	./stage3/chibicc -Iinclude -Itest -c -o $@ - < test/common

stage3/test/%: test/%.c stage3/chibicc
	./stage3/chibicc -Iinclude -Itest -c -o stage3/test/$*.o test/$*.c
	./stage3/chibicc -pthread -o $@ stage3/test/$*.o stage3/test/common.o

$(TESTS3): $(TEST_SRCS) test/test.h stage3/test/common.o stage3/chibicc stage3/test/libc-bootstrap.dll

test-stage3: $(TESTS3)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./stage3/chibicc

# Misc.

clean:
	rm -rf chibicc tmp*
	rm -rf test/*.cp test/*.o test/*.exe test/*.dll test/*.exestage?
	rm -rf $(TESTS1) $(TESTS2) $(TESTS3)
	rm -rf stage2 stage3
	find * -type f '(' -name '*~' -o -name '*.o' -o -name '*.runtimeconfig.json' ')' -exec rm {} ';'

.PHONY: test test-stage2 test-stage3 clean

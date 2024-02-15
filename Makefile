.SUFFIXES:

AS=chibias
#AS=../chibias-cil/chibias/bin/Debug/net8.0/chibias

CCFLAGS=-march=any
#CCFLAGS=-march=m32
#CCFLAGS=-march=m64
#CCFLAGS=-march=native

CFLAGS=-std=c11 -g -fno-common
#ASFLAGS=-f net6.0 -a $(DOTNET_ROOT)/sdk/6.0.418/AppHostTemplate/apphost -L$(DOTNET_ROOT)/shared/Microsoft.NETCore.App/6.0.26 -lSystem.Private.CoreLib

LIBC=../libc-cil/libc-bootstrap/bin/Debug/netstandard2.0/libc-bootstrap.dll

SRCS=$(wildcard ./*.c)
TEST_SRCS=$(wildcard test/*.c)

# Stage 1

OBJS1=$(SRCS:%.c=%.o)

./%.o: ./%.c
	$(CC) $(CFLAGS) -c -o $@ $*.c

./chibicc: $(OBJS1)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJS1): $(SRCS) chibicc.h

TESTS1=$(TEST_SRCS:.c=)

test/libc-bootstrap.dll: $(LIBC)
	cp $(LIBC) -t test

test/common.o: test/common test/test.h ./chibicc
	$(CC) -o- -E -P -C -xc test/common > test/common.cp
	./chibicc $(CCFLAGS) -c -o $@ test/common.cp

test/macro: chibicc test/macro.c test/common.o
	./chibicc $(CCFLAGS) -c -o test/macro.o test/macro.c
	./chibicc $(CCFLAGS) -o $@ test/macro.o test/common.o

test/%: test/%.c
	$(CC) -o- -E -P -C test/$*.c > test/$*.cp
	./chibicc $(CCFLAGS) -c -o test/$*.o test/$*.cp
	./chibicc $(CCFLAGS) -o $@ test/$*.o test/common.o

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

stage2/%.o: ./%.c
	mkdir -p stage2
	./self.py chibicc.h $< > stage2/$<
	./chibicc $(CCFLAGS) -c -o $@ stage2/$<

stage2/chibicc: $(OBJS2)
	./chibicc $(CCFLAGS) -o $@ $^

$(OBJS2): $(SRCS) chibicc.h ./self.py ./chibicc stage2/libc-bootstrap.dll

TESTS2=$(TEST_SRCS:%.c=stage2/%)

stage2/test/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage2/test
	cp $(LIBC) -t stage2/test

stage2/test/common.o: test/common test/test.h stage2/chibicc
	mkdir -p stage2/test
	$(CC) -o- -E -P -C -xc test/common > stage2/test/common.cp
	./stage2/chibicc $(CCFLAGS) -c -o $@ stage2/test/common.cp

stage2/test/macro: stage2/chibicc test/macro.c
	mkdir -p stage2/test
	./stage2/chibicc $(CCFLAGS) -c -o stage2/test/macro.o test/macro.c
	./stage2/chibicc $(CCFLAGS) -o $@ stage2/test/macro.o stage2/test/common.o

stage2/test/%: test/%.c
	$(CC) -o- -E -P -C test/$*.c > stage2/test/$*.cp
	./stage2/chibicc $(CCFLAGS) -c -o stage2/test/$*.o stage2/test/$*.cp
	./stage2/chibicc $(CCFLAGS) -o $@ stage2/test/$*.o stage2/test/common.o

$(TESTS2): $(TEST_SRCS) test/test.h stage2/test/common.o stage2/chibicc stage2/test/libc-bootstrap.dll

test-stage2: $(TESTS2)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./stage2/chibicc

# Stage 3

OBJS3=$(SRCS:%.c=stage3/%.o)

stage3/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage3
	cp $(LIBC) -t stage3

stage3/%.o: ./%.c
	mkdir -p stage3
	./self.py chibicc.h $< > stage3/$<
	./stage2/chibicc $(CCFLAGS) -c -o $@ stage3/$<

stage3/chibicc: $(OBJS3)
	./stage2/chibicc $(CCFLAGS) -o $@ $^

$(OBJS3): $(SRCS) chibicc.h ./self.py ./stage2/chibicc stage3/libc-bootstrap.dll

TESTS3=$(TEST_SRCS:%.c=stage3/%)

stage3/test/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage3/test
	cp $(LIBC) -t stage3/test

stage3/test/common.o: test/common test/test.h stage3/chibicc
	mkdir -p stage3/test
	$(CC) -o- -E -P -C -xc test/common > stage3/test/common.cp
	./stage3/chibicc $(CCFLAGS) -c -o $@ stage3/test/common.cp

stage3/test/%: test/%.c
	$(CC) -o- -E -P -C test/$*.c > stage3/test/$*.cp
	./stage3/chibicc $(CCFLAGS) -c -o stage3/test/$*.o stage3/test/$*.cp
	./stage3/chibicc $(CCFLAGS) -o $@ stage3/test/$*.o stage3/test/common.o

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

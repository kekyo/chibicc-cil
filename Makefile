.SUFFIXES:

#AS=chibias
AS=../chibias-cil/chibias/bin/Debug/net8.0/chibias

CCFLAGS=-march=any
#CCFLAGS=-march=m32
#CCFLAGS=-march=m64
#CCFLAGS=-march=native

CFLAGS=-std=c11 -g -fno-common
ASFLAGS=-f net45 -w x86 -r test/mscorlib.dll

LIBC=../libc-cil/libc-bootstrap/bin/Debug/net45/libc-bootstrap.dll

SRCS=$(wildcard ./*.c)
TEST_SRCS=$(wildcard test/*.c)

# Stage 1

OBJS1=$(SRCS:%.c=%.o)

./%.o: ./%.c
	$(CC) $(CFLAGS) -c -o $@ $*.c

./chibicc: $(OBJS1)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJS1): $(SRCS) chibicc.h

TESTS1=$(TEST_SRCS:.c=.exe)

test/libc-bootstrap.dll: $(LIBC)
	cp $(LIBC) -t test

test/common.s: test/common test/test.h ./chibicc
	$(CC) -o- -E -P -C -xc test/common > test/common.cp
	./chibicc $(CCFLAGS) -o $@ test/common.cp

test/%.exe: test/%.c
	$(CC) -o- -E -P -C test/$*.c > test/$*.cp
	./chibicc $(CCFLAGS) -o test/$*.s test/$*.cp
	$(AS) $(ASFLAGS) -r test/libc-bootstrap.dll -o $@ test/$*.s test/common.s

$(TESTS1): $(TEST_SRCS) test/test.h test/common.s ./chibicc test/libc-bootstrap.dll

test: $(TESTS1)
#	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	for i in $^; do echo $$i; mono ./$$i || exit 1; echo; done
	test/driver.sh ./chibicc

test-all: test test-stage2 test-stage3

# Stage 2

OBJS2=$(SRCS:%.c=stage2/%.s)

stage2/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage2
	cp $(LIBC) -t stage2

stage2/%.s: ./%.c
	mkdir -p stage2
	./self.py chibicc.h $< > stage2/$<
	./chibicc $(CCFLAGS) -o $@ stage2/$<

stage2/chibicc.exe: $(OBJS2)
	$(AS) $(ASFLAGS) -r stage2/libc-bootstrap.dll -o $@ $^

$(OBJS2): $(SRCS) chibicc.h ./self.py ./chibicc stage2/libc-bootstrap.dll

TESTS2=$(TEST_SRCS:%.c=stage2/%.exe)

stage2/test/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage2/test
	cp $(LIBC) -t stage2/test

stage2/test/common.s: test/common test/test.h stage2/chibicc.exe 
	mkdir -p stage2/test
	$(CC) -o- -E -P -C -xc test/common > stage2/test/common.cp
	mono ./stage2/chibicc.exe $(CCFLAGS) -o $@ stage2/test/common.cp

stage2/test/%.exe: test/%.c
	$(CC) -o- -E -P -C test/$*.c > stage2/test/$*.cp
	mono ./stage2/chibicc.exe $(CCFLAGS) -o stage2/test/$*.s stage2/test/$*.cp
	$(AS) $(ASFLAGS) -r stage2/test/libc-bootstrap.dll -o $@ stage2/test/$*.s stage2/test/common.s

$(TESTS2): $(TEST_SRCS) test/test.h stage2/test/common.s stage2/chibicc.exe stage2/test/libc-bootstrap.dll

test-stage2: $(TESTS2)
#	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	for i in $^; do echo $$i; mono ./$$i || exit 1; echo; done
	test/driver.sh "mono stage2/chibicc.exe"

# Stage 3

OBJS3=$(SRCS:%.c=stage3/%.s)

stage3/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage3
	cp $(LIBC) -t stage3

stage3/%.s: ./%.c
	mkdir -p stage3
	./self.py chibicc.h $< > stage3/$<
	mono ./stage2/chibicc.exe $(CCFLAGS) -o $@ stage3/$<

stage3/chibicc.exe: $(OBJS3)
	$(AS) $(ASFLAGS) -r stage3/libc-bootstrap.dll -o $@ $^

$(OBJS3): $(SRCS) chibicc.h ./self.py ./stage2/chibicc.exe stage3/libc-bootstrap.dll

TESTS3=$(TEST_SRCS:%.c=stage3/%.exe)

stage3/test/libc-bootstrap.dll: $(LIBC)
	mkdir -p stage3/test
	cp $(LIBC) -t stage3/test

stage3/test/common.s: test/common test/test.h stage3/chibicc.exe 
	mkdir -p stage3/test
	$(CC) -o- -E -P -C -xc test/common > stage3/test/common.cp
	mono ./stage3/chibicc.exe $(CCFLAGS) -o $@ stage3/test/common.cp

stage3/test/%.exe: test/%.c
	$(CC) -o- -E -P -C test/$*.c > stage3/test/$*.cp
	mono ./stage3/chibicc.exe $(CCFLAGS) -o stage3/test/$*.s stage3/test/$*.cp
	$(AS) $(ASFLAGS) -r stage3/test/libc-bootstrap.dll -o $@ stage3/test/$*.s stage3/test/common.s

$(TESTS3): $(TEST_SRCS) test/test.h stage3/test/common.s stage3/chibicc.exe stage3/test/libc-bootstrap.dll

test-stage3: $(TESTS3)
#	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	for i in $^; do echo $$i; mono ./$$i || exit 1; echo; done
	test/driver.sh "mono stage3/chibicc.exe"

# Misc.

clean:
	rm -rf chibicc tmp*
	rm -rf test/*.cp test/*.s test/libc-bootstrap.dll test/*.exe stage?
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean test-stage2

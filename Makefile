AS=chibias

CCFLAGS=-march=any
#CCFLAGS=-march=m32
#CCFLAGS=-march=m64
#CCFLAGS=-march=native

CFLAGS=-std=c11 -g -fno-common
ASFLAGS=-f net45 -w x86 -r test/mscorlib.dll

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.exe)

chibicc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): chibicc.h

test/libc-bootstrap.dll: ../libc-cil/libc-bootstrap/bin/Debug/net45/libc-bootstrap.dll
	cp ../libc-cil/libc-bootstrap/bin/Debug/net45/libc-bootstrap.dll -t test

test/common.s: chibicc test/common test/test.h
	$(CC) -o- -E -P -C -xc test/common > test/common.cp
	./chibicc $(CCFLAGS) -o $@ test/common.cp

test/%.exe: chibicc test/%.c test/test.h test/common.s test/libc-bootstrap.dll
	$(CC) -o- -E -P -C test/$*.c > test/$*.cp
	./chibicc $(CCFLAGS) -o test/$*.s test/$*.cp
	$(AS) $(ASFLAGS) -r test/libc-bootstrap.dll -o $@ test/$*.s test/common.s

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	for i in $^; do echo $$i; mono ./$$i || exit 1; echo; done
	test/driver.sh

clean:
	rm -rf chibicc tmp* $(TESTS) test/*.cp test/*.s test/*.exe test/libc-bootstrap.dll
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean

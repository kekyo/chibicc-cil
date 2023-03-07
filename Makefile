AS=chibias

CFLAGS=-std=c11 -g -fno-common
ASFLAGS=-f net45 -w x86 -r test/mscorlib.dll -r test/common.dll

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.exe)

chibicc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): chibicc.h

test/common.s: chibicc test/common
	$(CC) -o- -E -P -C -xc test/common | ./chibicc -o $@ -

test/%.exe: chibicc test/%.c test/common.s
	$(CC) -o- -E -P -C test/$*.c | ./chibicc -o test/$*.s -
	$(AS) $(ASFLAGS) -o $@ test/$*.s test/common.s

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	for i in $^; do echo $$i; mono ./$$i || exit 1; echo; done
	test/driver.sh

clean:
	rm -rf chibicc tmp* $(TESTS) test/*.s test/*.exe
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean

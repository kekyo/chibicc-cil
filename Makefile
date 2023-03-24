AS=chibias
CSC=mcs -unsafe+

MMODEL=
#MMODEL=-DM32
#MMODEL=-DM64

CFLAGS=-std=c11 -g -fno-common $(MMODEL)
ASFLAGS=-f net45 -w x86 -r test/mscorlib.dll

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.exe)

chibicc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): chibicc.h

test/common.dll: test/common.cs
	$(CSC) -t:library -out:$@ $<

test/common.s: chibicc test/common
	$(CC) -o- -E -P -C -xc test/common | ./chibicc -o $@ -

test/%.exe: chibicc test/%.c test/common.s test/common.dll
	$(CC) -o- -E -P -C test/$*.c | ./chibicc -o test/$*.s -
	$(AS) $(ASFLAGS) -r test/common.dll -o $@ test/$*.s test/common.s

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	for i in $^; do echo $$i; mono ./$$i || exit 1; echo; done
	test/driver.sh

clean:
	rm -rf chibicc tmp* $(TESTS) test/*.s test/*.exe
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean

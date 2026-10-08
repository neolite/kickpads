CFLAGS = -O2 -Wall -Wextra

theremin: theremin.c touch.c touch.h
	clang $(CFLAGS) -o $@ theremin.c touch.c -framework CoreFoundation -framework AudioToolbox

run: theremin
	./theremin --debug

clean:
	rm -f theremin

.PHONY: run clean

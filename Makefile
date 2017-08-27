objs = distlock.o testlock.o

#DEBUG_FLAGS = -g
DEBUG_FLAGS =
SHARED_FLAGS = -fPIC
OFLAGS = -O2
#CFLAGS = $(DEBUG_FLAGS) $(SHARED_FLAGS)
CFLAGS = $(DEBUG_FLAGS) $(SHARED_FLAGS) $(OFLAGS)

all: testlock

.c.o:
	gcc $(CFLAGS) -c $< -o $@ -lhiredis

libdistlock.a: $(objs)
	ar r $@ $^

libdistlock.so: $(objs)
	gcc -shared -o $@ $^

testlock: libdistlock.a
	gcc $(DEBUG_FLAGS) $(OFLAGS) $^ -o $@ -lhiredis libdistlock.a
	#gcc $(DEBUG_FLAGS) $(OFLAGS) $^ -o $@ -Wl,-rpath=. -L. -lhiredis -ldistlock

clean:
	rm -f $(objs)
	rm -f libdistlock.a
	rm -f libdistlock.so
	rm -f testlock

.PHONY: clean



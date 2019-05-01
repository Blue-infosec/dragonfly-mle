# ------------------------------------------------------------------
# dragonfly build
# ------------------------------------------------------------------
CC = $(BSD_CC)
CFLAGS = $(BSD_CFLAGS)
GIT_VERSION != git describe --abbrev=4 --always --tags

# ------------------------------------------------------------------
#
# ------------------------------------------------------------------
LINUX_CC = gcc
LINUX_CFLAGS = -g -O2 -Wall -march=native -Werror
BSD_CC = cc
BSD_CFLAGS = -O2 -Wall -march=native -Werror -Wno-error=tautological-pointer-compare

INCLUDES = -I. -I/usr/local/include -I./io -I./test -I./benchmark \
	   -I./lib/lua-cjson -I./lib/lua-hiredis -I./lib/lua-marshal  \

LIBS = -lpthread -lluajit-5.1 -lhiredis -lm -lcurl -lrt -lz -lmicrohttpd
LFLAGS = -L. -L/usr/local/lib


DFLAGS = -DMLE_VERSION=\"$(GIT_VERSION)\"

RM=/bin/rm
DRAGONFLY_ROOT=/usr/local/dragonfly-mle

# -----------------------------------------------------------------
# core files
# -----------------------------------------------------------------
MAIN_SRC = main.c
UNIT_SRC = unit-test.c
BENCH_SRC = main-bench.c

# -----------------------------------------------------------------
# library core files
# -----------------------------------------------------------------
SRCS0 = dragonfly-lib.c dragonfly-io.c dragonfly-cmds.c config.c \
	webservice.c msgqueue.c

# ------------------------------------------------------------------
# I/O modules
# ------------------------------------------------------------------
SRCS1 = io/io-file.c io/io-pipe.c io/io-tail.c io/io-syslog.c \
        io/io-zfile.c io/io-nats.c

# ------------------------------------------------------------------
# third party open-source libraries
# ------------------------------------------------------------------
SRCS3 = lib/lua-hiredis/lua-hiredis.c lib/lua-cjson/lua-cjson.c \
        lib/lua-cjson/strbuf.c lib/lua-cjson/fpconv.c \
        lib/lua-marshal/lmarshal.c

# ------------------------------------------------------------------
# unit test files
# ------------------------------------------------------------------
SRCS4 = test/test.c test/test0.c test/test1.c test/test2.c \
		test/test3.c test/test4.c test/test5.c test/test6.c \
		test/test7.c test/test8.c test/test9.c test/test10.c

# ------------------------------------------------------------------
# benchmarking files
# ------------------------------------------------------------------
SRCS5 = benchmark/benchmark.c benchmark/bench0.c benchmark/bench1.c benchmark/bench2.c benchmark/bench3.c


MAIN_OBJS = $(MAIN_SRC:.c=.o) $(SRCS0:.c=.o) $(SRCS1:.c=.o) $(SRCS2:.c=.o) $(SRCS3:.c=.o)
TEST_OBJS = $(UNIT_SRC:.c=.o) $(SRCS0:.c=.o) $(SRCS1:.c=.o) $(SRCS2:.c=.o) $(SRCS3:.c=.o) $(SRCS4:.c=.o)
BENCH_OBJS = $(BENCH_SRC:.c=.o) $(SRCS0:.c=.o) $(SRCS1:.c=.o) $(SRCS2:.c=.o) $(SRCS3:.c=.o) $(SRCS5:.c=.o)

MAIN = dragonfly-mle
TEST_BIN = dragonfly-test
BENCH_BIN = dragonfly-bench

all: $(MAIN)
test: $(TEST_BIN)
bench: $(BENCH_BIN)

# ------------------------------------------------------------------
# production build
# ------------------------------------------------------------------

$(MAIN): $(MAIN_OBJS)
	echo $(MLE_VERSION)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN) $(MAIN_OBJS) $(DFLAGS) $(LFLAGS) $(LIBS)

# ------------------------------------------------------------------
# test build
# ------------------------------------------------------------------
$(TEST_BIN): $(TEST_OBJS)
	echo $(MLE_VERSION)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TEST_BIN) $(TEST_OBJS) $(DFLAGS) $(LFLAGS) $(LIBS)
#	valgrind --leak-check=yes ./$(TEST_BIN) -r /tmp


# ------------------------------------------------------------------
# benchmarking build
# ------------------------------------------------------------------
$(BENCH_BIN): $(BENCH_OBJS)
	echo $(MLE_VERSION)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(BENCH_BIN) $(BENCH_OBJS) $(DFLAGS) $(LFLAGS) $(LIBS)


# ------------------------------------------------------------------
# install
# ------------------------------------------------------------------

install:
	@echo installing files
	install -d $(DRAGONFLY_ROOT)/bin/
	install -d $(DRAGONFLY_ROOT)/config/
	install -d $(DRAGONFLY_ROOT)/filter/
	install -d $(DRAGONFLY_ROOT)/analyzer/
	install -d $(DRAGONFLY_ROOT)/www/
	install ./dragonfly-mle $(DRAGONFLY_ROOT)/bin/
	install ./analyzer/* $(DRAGONFLY_ROOT)/analyzer/
	install ./filter/* $(DRAGONFLY_ROOT)/filter/
	install ./config/* $(DRAGONFLY_ROOT)/config/
	install ./www/* $(DRAGONFLY_ROOT)/www/

# ------------------------------------------------------------------
# reconfigure OS target, i.e. gmake for Linux and make for FreeBSD
# ------------------------------------------------------------------
target:
	@./make-target.sh

# ------------------------------------------------------------------
# misc
# ------------------------------------------------------------------
.c.o:
	$(CC) $(CFLAGS) $(DFLAGS) $(INCLUDES) -c $<  -o $@

.PHONY: test clean

clean:
	$(RM) -f $(MAIN_OBJS) $(TEST_OBJS) $(BENCH_OBJS) $(MAIN) $(DEBUG_BIN) $(TEST_BIN) $(BENCH_BIN)

cmake_minimum_required(VERSION 3.14)
project(dragonfly_mle C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_FLAGS "-O2 -Wall -march=native -Werror")

set(SOURCE_FILES
        msgqueue.c
        msgqueue.h
        io/io-dummy.c
        io/io-file.c
        io/io-file.h
        io/io-pipe.c
        io/io-pipe.h
        io/io-syslog.c
        io/io-syslog.h
        io/io-tail.c
        io/io-tail.h
        io/io-zfile.c
        io/io-zfile.h
        lib/lua-cjson/fpconv.c
        lib/lua-cjson/fpconv.h
        lib/lua-cjson/lua-cjson.c
        lib/lua-cjson/lua-cjson.h
        lib/lua-cjson/strbuf.c
        lib/lua-cjson/strbuf.h
        lib/lua-hiredis/lua-hiredis.c
        lib/lua-hiredis/lua-hiredis.h
        config.c
        config.h
        dragonfly-cmds.c
        dragonfly-cmds.h
        dragonfly-io.c
        dragonfly-io.h
        dragonfly-lib.c
        dragonfly-lib.h
        param.h
        webservice.c
        webservice.h)

include_directories(src)
include_directories(io)
include_directories(lib/libipfix/lib)
include_directories(lib/libipfix/libmisc)
include_directories(lib/libipfix/probe)
include_directories(lib/libipfix/tools/reexporter)
include_directories(lib/libipfix/tools/reexporter/ipfixtofile)
include_directories(lib/lua-cjson)
include_directories(lib/lua-cmsgpack)
include_directories(lib/lua-hiredis)
include_directories(lib/msgqueue)
include_directories(resp)
include_directories(test)
include_directories(benchmark)

add_executable(dragonfly-test
        unit-test.c
        test/test.h
        test/test.c
        test/test0.c
        test/test1.c
        test/test2.c
        test/test3.c
        test/test4.c
        test/test5.c
        test/test6.c
        test/test7.c
        test/test8.c
        test/test9.c
        test/test10.c
        ${SOURCE_FILES})

add_executable(dragonfly-bench
        main-bench.c
        benchmark/benchmark.c
        benchmark/benchmark.h
        benchmark/bench0.c
        benchmark/bench1.c
        benchmark/bench2.c
        benchmark/bench3.c
        benchmark/bench4.c
        benchmark/bench5.c
        benchmark/bench6.c
        ${SOURCE_FILES})

add_executable(dragonfly-mle
        main.c
        ${SOURCE_FILES})

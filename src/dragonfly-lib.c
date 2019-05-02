/* Copyright (C) 2017-2018 CounterFlow AI, Inc.
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 *
 * author Randy Caldejon <rc@counterflowai.com>
 */

#include <pwd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <syslog.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <sys/wait.h>

#include <luajit-2.0/lauxlib.h>
#include <luajit-2.0/lualib.h>
#include <luajit-2.0/luajit.h>

#include "lua-hiredis.h"
#include "lua-cjson.h"
#include "lmarshal.h"

#include "msgqueue.h"

#include "dragonfly-lib.h"
#include "dragonfly-cmds.h"
#include "dragonfly-io.h"
#include "webservice.h"
#include "config.h"
#include "param.h"

extern int g_verbose;
extern int g_chroot;
extern int g_drop_priv;

uint64_t volatile g_running = 1;
uint64_t volatile g_initialized = 0;

static char g_root_dir[PATH_MAX];
static char g_log_dir[PATH_MAX];
static char g_filter_dir[PATH_MAX+PATH_MAX];
static char g_analyzer_dir[PATH_MAX+PATH_MAX];
static char g_config_file[PATH_MAX+PATH_MAX];

static int g_analyzer_pid = -1;
static int g_num_analyzer_threads = 0;
static int g_num_input_threads = 0;
static int g_num_flywheel_threads = 0;
static int g_num_output_threads = 0;

static pthread_barrier_t g_flywheel_barrier;
static pthread_barrier_t g_output_barrier;

char *g_redis_host = NULL;
int g_redis_port = 6379;

#define ROTATE_MESSAGE "+rotate+"

static MLE_STATS *g_stats = NULL;
static INPUT_CONFIG g_input_list[MAX_INPUT_STREAMS];
static INPUT_CONFIG g_flywheel_list[MAX_INPUT_STREAMS];
static OUTPUT_CONFIG g_output_list[MAX_OUTPUT_STREAMS];
static ANALYZER_CONFIG g_analyzer_list[MAX_ANALYZER_STREAMS];

static pthread_t g_io_thread[(MAX_INPUT_STREAMS * 2) + MAX_OUTPUT_STREAMS];
static pthread_t g_analyzer_thread[MAX_ANALYZER_STREAMS + 1];
static pthread_mutex_t g_timer_lock = PTHREAD_MUTEX_INITIALIZER;

static MLE_TIMER g_timer_list[MAX_ANALYZER_STREAMS];

#define VERBOSE_PRINT(x) \
    if (g_verbose)       \
    fprintf

int timer_event(lua_State *L);
int analyze_event(lua_State *L);
int forward_event(lua_State *L);
int log_event(lua_State *L);
int stats_event(lua_State *L);

/* list of functions in the module */
static const luaL_reg dragonfly_functions[] =
    {{"date2epoch", dragonfly_date2epoch},
     {"http_get", dragonfly_http_get},
     {"dnslookup", dragonfly_dnslookup},
     {"echo", dragonfly_echo},
     {"timer_event", timer_event},
     {"analyze_event", analyze_event},
     {"output_event", forward_event},
     {"forward_event", forward_event},
     {"log_event", log_event},
     {"stats_event", stats_event},
     {NULL, NULL}};

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int luaopen_dragonfly_functions(lua_State *L)
{
    luaL_register(L, "dragonfly", dragonfly_functions);
    return 1;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void signal_abort(int signum)
{
    g_running = 0;
    syslog(LOG_INFO, "%s", __FUNCTION__);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void verbose_print(const char *format, ...)
{
    if (g_verbose)
    {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}
/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
static void lua_disable_io(lua_State *L)
{
    /*
     * Disable I/O in the loop() entry point. Reduces security risk.
     */
    lua_pushnil(L);
    lua_setglobal(L, "io");
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void signal_term(int signum)
{
    syslog(LOG_INFO, "%s", __FUNCTION__);
    g_running = 0;
    if (g_analyzer_pid > 0)
    {
        // tell child process to shutdown
        kill(g_analyzer_pid, SIGINT);
    }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void signal_log_rotate(int signum)
{
    syslog(LOG_INFO, "%s", __FUNCTION__);
    for (int i = 0; g_output_list[i].tag != NULL; i++)
    {
        msgqueue_send(g_output_list[i].queue, ROTATE_MESSAGE, strlen(ROTATE_MESSAGE));
    }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int timer_event(lua_State *L)
{
    if (lua_gettop(L) != 3)
    {
        return luaL_error(L, "expecting exactly 3 arguments");
    }
    const char *tag = luaL_checkstring(L, 1);
    const int future_seconds = lua_tointeger(L, 2);
    for (int i = 0; g_timer_list[i].tag != NULL; i++)
    {
        if (strcasecmp(tag, g_analyzer_list[i].tag) == 0)
        {
            lua_getglobal(L,"marshal");
            int index = lua_gettop(L);
            lua_getfield(L,index,"encode");
            lua_pushvalue(L, 3);
            if (lua_pcall(L, 1, 1, 0) == LUA_ERRRUN)
            {
                syslog(LOG_ERR, "%s: lua_pcall error : - %s", __FUNCTION__, lua_tostring(L, -1));
                lua_pop(L, 1);
                exit(EXIT_FAILURE);
            }

            const char *message = lua_tolstring(L, -1, &g_timer_list[i].length);

            pthread_mutex_lock(&g_timer_lock);
            g_timer_list[i].message = strndup(message, g_timer_list[i].length);
            g_timer_list[i].epoch = (time(NULL) + future_seconds);
            pthread_mutex_unlock(&g_timer_lock);

            lua_settop(L, index-1);
            return 0;
        }
    }
    return 0;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int analyze_event(lua_State *L)
{
    if (lua_gettop(L) != 2)
    {
        return luaL_error(L, "expecting exactly 2 arguments");
    }
    size_t len = 0;
    const char *name = luaL_checkstring(L, 1);

    for (int i = 0; g_analyzer_list[i].tag != NULL; i++)
    {
        if (strcasecmp(name, g_analyzer_list[i].tag) == 0)
        {
            lua_getglobal(L,"marshal");
            int index = lua_gettop(L);
            lua_getfield(L,index,"encode");
            lua_pushvalue(L, 2);
            if (lua_pcall(L, 1, 1, 0) == LUA_ERRRUN)
            {
                syslog(LOG_ERR, "%s: lua_pcall error : - %s", __FUNCTION__, lua_tostring(L, -1));
                lua_pop(L, 1);
                exit(EXIT_FAILURE);
            }

            const char *message = lua_tolstring(L, -1, &len);
            if (msgqueue_send(g_analyzer_list[i].queue, message, len) < 0)
            {
                syslog(LOG_ERR, "%s:  msgqueue_send() error - %i", __FUNCTION__, (int)len);
            }

            lua_settop(L, index-1);
            return 0;
        }
    }
    return 0;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * 
 * 
 * ---------------------------------------------------------------------------------------
 */
int log_event(lua_State *L)
{
    if (lua_gettop(L) != 1)
    {
        return luaL_error(L, "expecting exactly 1 arguments");
    }
    lua_getglobal(L,"cjson_safe");
    int index = lua_gettop(L);
    lua_getfield(L,index,"encode");
    lua_pushvalue(L, 1);

    if (lua_pcall(L, 1, 1, 0) == LUA_ERRRUN)
    {
        syslog(LOG_ERR, "log_event: lua_pcall error : - %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        exit(EXIT_FAILURE);
    }
        
    size_t len = 0;
    const char *message = lua_tolstring(L, -1, &len);

    msgqueue_send(g_output_list[DRAGONFLY_LOG_INDEX].queue, message, len);
    lua_settop (L, index-1);

    return 0;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * 
 * 
 * ---------------------------------------------------------------------------------------
 */
int stats_event(lua_State *L)
{
    if (lua_gettop(L) != 1)
    {
        return luaL_error(L, "expecting exactly 1 arguments");
    }
    lua_getglobal(L,"cjson_safe");
    int index = lua_gettop(L);
    lua_getfield(L,index,"encode");
    lua_pushvalue(L, 1);

    if (lua_pcall(L, 1, 1, 0) == LUA_ERRRUN)
    {
        syslog(LOG_ERR, "stats_event: lua_pcall error : - %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        exit(EXIT_FAILURE);
    }

    size_t len = 0;
    const char *message = lua_tolstring(L, -1, &len);
    msgqueue_send(g_output_list[DRAGONFLY_STATS_INDEX].queue, message, len);

    lua_settop (L, index-1);

    return 0;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * 
 * 
 * ---------------------------------------------------------------------------------------
 */
int forward_event(lua_State *L)
{
    if (lua_gettop(L) != 2)
    {
        return luaL_error(L, "expecting exactly 2 arguments");
    }
    const char *name = luaL_checkstring(L, 1);
    for (int i = 2; g_output_list[i].tag != NULL; i++)
    {
        if (strcasecmp(name, g_output_list[i].tag) == 0)
        {
            lua_getglobal(L,"cjson_safe");
            int index = lua_gettop(L);
            lua_getfield(L,index,"encode");
            lua_pushvalue(L, 2);

            if (lua_pcall(L, 1, 1, 0) == LUA_ERRRUN)
            {
                syslog(LOG_ERR, "cjson_safe.encode(): lua_pcall error : - %s", lua_tostring(L, -1));
                lua_pop(L, 1);
                exit(EXIT_FAILURE);
            }
        
            size_t len = 0;
            const char *message = lua_tolstring(L, -1, &len);
            msgqueue_send(g_output_list[i].queue, message, len);
	    lua_settop (L, index-1);
            return 0;
        }
    }

    return 0;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
static void *lua_timer_thread(void *ptr)
{

    time_t last_time = (time(NULL) - DEFAULT_STATS_INTERVAL);
    time_t now_time;

    pthread_detach(pthread_self());
#ifdef _GNU_SOURCE
    pthread_setname_np(pthread_self(), "timer");
#endif
    syslog(LOG_NOTICE, "Running %s\n", "timer");

    while (g_running)
    {
        sleep(1);
        pthread_mutex_lock(&g_timer_lock);
        for (int i = 0; g_timer_list[i].tag != NULL; i++)
        {
            if (g_timer_list[i].epoch > 0)
            {
                time_t now_time = time(NULL);
                if (now_time >= g_timer_list[i].epoch)
                {
                    if (msgqueue_send(g_timer_list[i].queue, g_timer_list[i].message, g_timer_list[i].length) < 0)
                    {
                        syslog(LOG_ERR, "%s:  msgqueue_send() error - %i", __FUNCTION__, (int)g_timer_list[i].length);
                    }
                    g_timer_list[i].epoch = 0;
                    g_timer_list[i].length = 0;
                    free(g_timer_list[i].message);
                    g_timer_list[i].message = NULL;
                }
            }
        }
        pthread_mutex_unlock(&g_timer_lock);
        /*
         * log ML engine stats every 5 minutes
         */
        now_time = time(NULL);
        if ((now_time - last_time) >= DEFAULT_STATS_INTERVAL)
        {
            char timestamp[256];
            char buffer[1024];
            strftime(timestamp, sizeof(timestamp), "%FT%TZ", gmtime(&now_time));
            snprintf(buffer, (sizeof(buffer) - 1),
                     "{ \"time\": \"%s\", \"operations\": { \"input\": %lu, \"analyzer\":%lu, \"output\":%lu }}",
                     timestamp, g_stats->input, g_stats->analysis, g_stats->output);
            last_time = now_time;
            msgqueue_send(g_output_list[DRAGONFLY_STATS_INDEX].queue, buffer, strlen(buffer));
        }
    }
    syslog(LOG_NOTICE, "%s exiting\n", "timer");
    return (void *)NULL;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void lua_flywheel_loop(INPUT_CONFIG *flywheel)
{
    int n = 0;
    char buffer[_MAX_BUFFER_SIZE_];

    while (g_running)
    {

        if ((n = dragonfly_io_read(flywheel->input, buffer, _MAX_BUFFER_SIZE_)) < 0)
        {
            if (g_running)
            {
                syslog(LOG_ERR, "%s: dragonfly_io_read() error", __FUNCTION__);
            }
            return;
        }
        /*
        else if (n==0)
        {
            syslog(LOG_ERR, "%s: dragonfly_io_read() zero", __FUNCTION__);
        }
        */
        else if (n == _MAX_BUFFER_SIZE_)
        {
            syslog(LOG_ERR, "%s: skipping message; exceeded buffer size of %d", __FUNCTION__, _MAX_BUFFER_SIZE_);
        }
        else
        {
            msgqueue_send(flywheel->queue, buffer, n);
        }
    }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
static void *lua_flywheel_thread(void *ptr)
{
    INPUT_CONFIG *flywheel = (INPUT_CONFIG *)ptr;

    pthread_detach(pthread_self());
#ifdef _GNU_SOURCE
    pthread_setname_np(pthread_self(), flywheel->tag);
#endif
    syslog(LOG_NOTICE, "Running %s\n", flywheel->tag);

    pthread_barrier_wait(&g_flywheel_barrier);
    while (g_running)
    {
        syslog(LOG_NOTICE, "%s: opening %s\n", flywheel->tag, flywheel->uri);
        if ((flywheel->input = dragonfly_io_open(flywheel->uri, DF_IN)) == NULL)
        {
            break;
        }
        lua_flywheel_loop(flywheel);
        dragonfly_io_close(flywheel->input);

        // if the source is a flat file, then exit
        if (dragonfly_io_isfile(flywheel->input))
        {
            break;
        }
    }
    if (flywheel->tag)
    {
        syslog(LOG_NOTICE, "%s exiting\n", flywheel->tag);
    }
    return (void *)NULL;
}
/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void lua_input_loop(lua_State *L, INPUT_CONFIG *input)
{
    int n;
    char buffer[_MAX_BUFFER_SIZE_];
    /*
     * Disable I/O in the loop() entry point.
     */
    lua_disable_io(L);
    while (g_running)
    {
        if ((n = msgqueue_recv(input->queue, buffer, _MAX_BUFFER_SIZE_)) <= 0)
        {
            return;
        }
        else if (n == _MAX_BUFFER_SIZE_)
        {
            syslog(LOG_ERR, "%s: skipping message; exceeded buffer size of %d", __FUNCTION__, _MAX_BUFFER_SIZE_);
        }
        else
        {
	    lua_getglobal(L,"cjson_safe");
	    int index = lua_gettop(L);
	    lua_getfield(L,index,"decode");
            lua_pushlstring(L, buffer, n);
            if (lua_pcall(L, 1, 1, 0) == LUA_ERRRUN)
	    {
                syslog(LOG_ERR, "cjson_safe: lua_pcall error : - %s", lua_tostring(L, -1));
                lua_pop(L, 1);
                exit(EXIT_FAILURE);
	    }

	    lua_insert (L, 4);
            lua_getglobal(L, "loop");
	    lua_insert (L, 4);
            lua_settop(L, 5);

            if (lua_pcall(L, 1, 0, 0) == LUA_ERRRUN)
            {
                syslog(LOG_ERR, "loop: lua_pcall error : - %s", lua_tostring(L, -1));
                lua_pop(L, 1);
                exit(EXIT_FAILURE);
            }
            lua_settop(L, index-1);
            g_stats->input++;
        }
    }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
static void *lua_input_thread(void *ptr)
{
    INPUT_CONFIG *input = (INPUT_CONFIG *)ptr;
    char *lua_script = input->script;

    pthread_detach(pthread_self());
#ifdef _GNU_SOURCE
    pthread_setname_np(pthread_self(), input->tag);
#endif
    /*
     * Set thread name to the file name of the lua script
     */
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    /*
     * Load the built-in dragonfly function table
     */
    luaopen_dragonfly_functions(L);

    /* set the "default" next hop for this analyzer */
    if (input->default_analyzer && strnlen(input->default_analyzer, 32) > 0)
    {
        lua_pushstring(L, input->default_analyzer);
        fprintf(stderr, "%s:  default_analyzer: %s\n", __FUNCTION__, input->default_analyzer);
        lua_setglobal(L, "default_analyzer");
    }

    /*
     * Load the lua-cjson library:
     * 
     *  https://github.com/mpx/lua-cjson
     * 
     */
    luaopen_cjson_safe(L);
    if (g_verbose)
    {
        syslog(LOG_INFO, "Loaded lua-cjson library");
        fprintf(stderr, "%s: loaded lua-cjson library\n", __FUNCTION__);
    }

    /*
     * Load LUA serialization library
     *
     * https://github.com/richardhundt/lua-marshal
     */
    luaopen_marshal (L);

    /*
     * Load the lua-hiredis library:
     * 
     *  https://github.com/agladysh/lua-hiredis.git
     * 
     */
    luaopen_hiredis(L, g_redis_host, g_redis_port);

    if (g_verbose)
    {
        syslog(LOG_INFO, "loaded lua-hiredis library");
        fprintf(stderr, "%s: loaded lua-hiredis library\n", __FUNCTION__);
    }

    if (luaL_loadfile(L, lua_script) || (lua_pcall(L, 0, 0, 0) == LUA_ERRRUN))
    {
        syslog(LOG_ERR, "luaL_loadfile %s failed - %s", lua_script, lua_tostring(L, -1));
        lua_pop(L, 1);
        exit(EXIT_FAILURE);
    }


    luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
    syslog(LOG_INFO, "Loaded %s", lua_script);

    /* initialize the script */
    lua_getglobal(L, "setup");
    if (lua_pcall(L, 0, 0, 0) == LUA_ERRRUN)
    {
        syslog(LOG_ERR, "%s error; %s", lua_script, lua_tostring(L, -1));
        lua_pop(L, 1);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_NOTICE, "Running %s\n", input->tag);

    while (g_running)
    {
        lua_input_loop(L, input);
    }

    lua_close(L);
    if (input->tag)
    {
        syslog(LOG_NOTICE, "%s exiting\n", input->tag);
    }
    return (void *)NULL;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

void lua_output_loop(OUTPUT_CONFIG *output)
{
    int n;
    char buffer[_MAX_BUFFER_SIZE_];
    while (g_running)
    {
        if ((n = msgqueue_recv(output->queue, buffer, _MAX_BUFFER_SIZE_)) <= 0)
        {
            return;
        }
        else if (n == _MAX_BUFFER_SIZE_)
        {
            syslog(LOG_ERR, "%s: skipping message; exceeded buffer size of %d", __FUNCTION__, _MAX_BUFFER_SIZE_);
        }
        else
        {
            buffer[n] = '\0';
            if (strcasecmp(buffer, ROTATE_MESSAGE) == 0)
            {
                dragonfly_io_rotate(output->output);
            }
            else
            {
                if (dragonfly_io_write(output->output, buffer) < 0)
                {
                    fprintf(stderr, "%s: output error\n", __FUNCTION__);
                    return;
                }
                if (g_stats)
                    g_stats->output++;
            }
        }
    }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
static void *lua_output_thread(void *ptr)
{
    OUTPUT_CONFIG *output = (OUTPUT_CONFIG *)ptr;

    pthread_detach(pthread_self());
#ifdef _GNU_SOURCE
    pthread_setname_np(pthread_self(), output->tag);
#endif

    syslog(LOG_NOTICE, "Running %s\n", output->tag);

    pthread_barrier_wait(&g_output_barrier);
    while (g_running)
    {
        if ((output->output = dragonfly_io_open(output->uri, DF_OUT)) == NULL)
        {
            break;
        }
        lua_output_loop(output);
        dragonfly_io_close(output->output);
    }

    if (output->tag)
    {
        syslog(LOG_NOTICE, "%s exiting\n", output->tag);
    }
    return (void *)NULL;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void lua_analyzer_loop(lua_State *L, ANALYZER_CONFIG *analyzer)
{

    int n;
    char buffer[_MAX_BUFFER_SIZE_];
    /*
     * Disable I/O in the loop() entry point.
     */
    lua_disable_io(L);

    while (g_running)
    {

        if ((n = msgqueue_recv(analyzer->queue, buffer, _MAX_BUFFER_SIZE_)) <= 0)
        {
            return;
        }
        else if (n == _MAX_BUFFER_SIZE_)
        {
            syslog(LOG_ERR, "%s: skipping message; exceeded buffer size of %d", __FUNCTION__, _MAX_BUFFER_SIZE_);
        }
        else
        {
            lua_getglobal(L,"marshal");
            int index = lua_gettop(L);
            lua_getfield(L,index,"decode");
            lua_pushlstring(L, buffer, n);
            if (lua_pcall(L, 1, 1, 0) == LUA_ERRRUN)
            {
                syslog(LOG_ERR, "%s: lua_pcall error : - %s", __FUNCTION__, lua_tostring(L, -1));
                lua_pop(L, 1);
                exit(EXIT_FAILURE);
            }

            lua_getglobal(L, "loop");
            lua_insert (L, -2);

            if (lua_pcall(L, 1, 0, 0) == LUA_ERRRUN)
            {
                syslog(LOG_ERR, "%s lua_pcall error: %s", __FUNCTION__, lua_tostring(L, -1));
                lua_pop(L, 1);
                exit(EXIT_FAILURE);
            }

            lua_settop(L, index-1);

            if (g_stats)
                g_stats->analysis++;
        }
    }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
static void *lua_analyzer_thread(void *ptr)
{
    ANALYZER_CONFIG *analyzer = (ANALYZER_CONFIG *)ptr;
    char *lua_script = analyzer->script;

    pthread_detach(pthread_self());
#ifdef _GNU_SOURCE
    pthread_setname_np(pthread_self(), analyzer->tag);
#endif
    /*
     * Set thread name to the file name of the lua script
     */
    lua_State *L = luaL_newstate();

    luaL_openlibs(L);

    if (luaL_loadfile(L, lua_script) || (lua_pcall(L, 0, 0, 0) == LUA_ERRRUN))
    {
        syslog(LOG_ERR, "luaL_loadfile %s failed - %s", lua_script, lua_tostring(L, -1));
        lua_pop(L, 1);
        exit(EXIT_FAILURE);
    }
    luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
    syslog(LOG_INFO, "Loaded %s", lua_script);
    /*
     * Load the built-in dragonfly function table
     */
    luaopen_dragonfly_functions(L);

    /*
     * Load the lua-cjson library:
     * 
     *  https://github.com/mpx/lua-cjson
     * 
     */
    luaopen_cjson_safe(L);
    if (g_verbose)
    {
        syslog(LOG_INFO, "Loaded lua-cjson library");
        fprintf(stderr, "%s: loaded lua-cjson library\n", __FUNCTION__);
    }
    
    /*
     * Load LUA serialization library
     *
     * https://github.com/richardhundt/lua-marshal
     */
    luaopen_marshal (L);

    /*
     * Load the lua-hiredis library:
     * 
     *  https://github.com/agladysh/lua-hiredis.git
     * 
     */
    luaopen_hiredis(L, g_redis_host, g_redis_port);
    if (g_verbose)
    {
        syslog(LOG_INFO, "loaded lua-hiredis library");
        fprintf(stderr, "%s: loaded lua-hiredis library\n", __FUNCTION__);
    }

    /* set the "default" next hop for this analyzer */
    if (analyzer->default_analyzer && strnlen(analyzer->default_analyzer, 32) > 0)
    {
        lua_pushstring(L, analyzer->default_analyzer);
        lua_setglobal(L, "default_analyzer");
    }

    /* set the "default" output */
    if (analyzer->default_output && strnlen(analyzer->default_output, 32))
    {
        lua_pushstring(L, analyzer->default_output);
        lua_setglobal(L, "default_output");
    }


    /* initialize the script */
    lua_getglobal(L, "setup");
    if (lua_pcall(L, 0, 0, 0) == LUA_ERRRUN)
    {
        syslog(LOG_ERR, "%s lua_pcall error:  %s", __FUNCTION__, lua_tostring(L, -1));
        lua_pop(L, 1);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_NOTICE, "Running %s\n", analyzer->tag);
    while (g_running)
    {
        lua_analyzer_loop(L, analyzer);
    }
    lua_close(L);

    if (analyzer->tag)
    {
        syslog(LOG_NOTICE, "%s exiting\n", analyzer->tag);
    }
    pthread_exit(NULL);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

void destroy_configuration()
{
    if (g_initialized)
    {
        g_initialized = 0;
        unload_inputs_config(g_input_list, MAX_INPUT_STREAMS);
        unload_outputs_config(g_output_list, MAX_OUTPUT_STREAMS);
        unload_analyzers_config(g_analyzer_list, MAX_ANALYZER_STREAMS);

        g_num_analyzer_threads = 0;
        g_num_input_threads = 0;
        g_num_flywheel_threads = 0;
        g_num_output_threads = 0;
        memset(g_analyzer_list, 0, sizeof(g_analyzer_list));
        memset(g_input_list, 0, sizeof(g_input_list));
        memset(g_output_list, 0, sizeof(g_output_list));
        memset(g_io_thread, 0, sizeof(g_io_thread));
    }
}
/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

void initialize_configuration(const char *rootdir, const char *logdir, const char *rundir)
{
    umask(022);
    g_verbose = isatty(1);
    g_num_analyzer_threads = 0;
    g_num_input_threads = 0;
    g_num_flywheel_threads = 0;
    g_num_output_threads = 0;
    memset(g_analyzer_list, 0, sizeof(g_analyzer_list));
    memset(g_input_list, 0, sizeof(g_input_list));
    memset(g_output_list, 0, sizeof(g_output_list));
    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_io_thread, 0, sizeof(g_io_thread));
    memset(g_analyzer_thread, 0, sizeof(g_analyzer_thread));

    // check root dir
    if (!*rootdir)
    {
        strncpy(g_root_dir, DRAGONFLY_ROOT_DIR, PATH_MAX-1);
    }
    else
    {
        strncpy(g_root_dir, rootdir, PATH_MAX-1);
    }
    struct stat sb;
    if ((lstat(g_root_dir, &sb) < 0) || !S_ISDIR(sb.st_mode))
    {
        fprintf(stderr, "DRAGONFLY_ROOT %s does not exist\n", g_root_dir);
        exit(EXIT_FAILURE);
    }

    // check log dir
    if (!logdir)
    {
        strncpy(g_log_dir, DRAGONFLY_LOG_DIR, PATH_MAX-1);
    }
    else
    {
        strncpy(g_log_dir, logdir, PATH_MAX-1);
    }
    dragonfly_io_set_logdir(g_log_dir);

    /*
	 * Make sure log directory exists
	 */
    if ((lstat(g_log_dir, &sb) < 0) || !S_ISCHR(sb.st_mode))
    {
        if (mkdir(g_log_dir, 0755) && errno != EEXIST)
        {
            fprintf(stderr, "mkdir (%s) - %s\n", g_log_dir, strerror(errno));
            syslog(LOG_ERR, "mkdir (%s) - %s\n", g_log_dir, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    snprintf(g_config_file, sizeof(g_config_file)-1, "%s/%s", g_root_dir, CONFIG_FILE);
    if ((lstat(g_config_file, &sb) < 0) || !S_ISREG(sb.st_mode))
    {
        fprintf(stderr, "config file %s does not exist.\n", g_config_file);
        syslog(LOG_ERR, "config file %s does not exist.\n", g_config_file);
        exit(EXIT_FAILURE);
    }

    snprintf(g_analyzer_dir, sizeof(g_analyzer_dir)-1, "%s/%s", g_root_dir, ANALYZER_DIR);
    /*
	 * Make sure analyzer directory exists
	 */
    if ((lstat(g_analyzer_dir, &sb) < 0) || !S_ISDIR(sb.st_mode))
    {
        fprintf(stderr, "analyzer directory %s does not exist.\n", g_analyzer_dir);
        syslog(LOG_ERR, "analyzer directory %s does not exist.\n", g_analyzer_dir);
        exit(EXIT_FAILURE);
    }

    snprintf(g_filter_dir, sizeof(g_filter_dir)-1, "%s/%s", g_root_dir, FILTER_DIR);
    /*
	 * Make sure filter directory exists
	 */
    if ((lstat(g_filter_dir, &sb) < 0) || !S_ISDIR(sb.st_mode))
    {
        fprintf(stderr, "filter directory %s does not exist.\n", g_filter_dir);
        syslog(LOG_ERR, "filter directory %s does not exist.\n", g_filter_dir);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "version: %s\n", MLE_VERSION);
    if (g_verbose)
        fprintf(stderr, "version: %s\n", g_log_dir);
    syslog(LOG_INFO, "log dir: %s\n", g_log_dir);
    if (g_verbose)
        fprintf(stderr, "log dir: %s\n", g_log_dir);
    syslog(LOG_INFO, "analyzer dir: %s\n", g_analyzer_dir);
    fprintf(stderr, "analyzer dir: %s\n", g_analyzer_dir);
    if (g_verbose)
        syslog(LOG_INFO, "config file: %s\n", g_config_file);
    if (g_verbose)
        fprintf(stderr, "config file: %s\n", g_config_file);

    lua_State *L = luaL_newstate();
    /*
     * Load config.lua
     */
    if (luaL_loadfile(L, g_config_file))
    {
        syslog(LOG_ERR, "luaL_loadfile failed; %s", lua_tostring(L, -1));
        exit(EXIT_FAILURE);
    }
    if (lua_pcall(L, 0, 0, 0) == LUA_ERRRUN)
    {
        syslog(LOG_ERR, "%s lua_pcall error: %s", __FUNCTION__, lua_tostring(L, -1));
        lua_pop(L, 1);
        exit(EXIT_FAILURE);
    }
    lua_getglobal(L, "redis_port");
    if (lua_isstring(L, -1))
    {
        g_redis_port = atoi(lua_tostring(L, -1));
    }
    lua_getglobal(L, "redis_host");
    if (lua_isstring(L, -1))
    {
        g_redis_host = strdup(lua_tostring(L, -1));
    }
    else
    {
        g_redis_host = strdup("127.0.0.1");
    }
    if (load_redis(L, g_redis_host, g_redis_port) < 0)
    {
        syslog(LOG_ERR, "load_redis failed");
    }
    if ((g_num_analyzer_threads = load_analyzers_config(L, g_analyzer_dir, g_analyzer_list, MAX_ANALYZER_STREAMS)) <= 0)
    {
        syslog(LOG_ERR, "load_analyzer_config failed");
        exit(EXIT_FAILURE);
    }
    if ((g_num_output_threads = load_outputs_config(L, g_output_list, MAX_OUTPUT_STREAMS)) <= 0)
    {
        syslog(LOG_ERR, "load_output_config failed");
        exit(EXIT_FAILURE);
    }
    if ((g_num_input_threads = load_inputs_config(L, g_filter_dir, g_input_list, MAX_INPUT_STREAMS)) <= 0)
    {
        syslog(LOG_ERR, "load_input_config failed");
        exit(EXIT_FAILURE);
    }
    lua_close(L);
    g_initialized = 1;
}
/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

static void process_drop_privilege()
{
    if (setgid(getgid()) < 0)
    {
        syslog(LOG_ERR, "setgid: %s", strerror(errno));
    }
    struct passwd *pwd = getpwnam(USER_NOBODY);
    if (pwd && setuid(pwd->pw_uid) != 0)
    {
        syslog(LOG_ERR, "setuid(%s): %s", USER_NOBODY, strerror(errno));
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "dropped privileges: %s\n", USER_NOBODY);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

void launch_analyzer_process(const char *dragonfly_analyzer_root)
{
    int n = 0;

    for (int i = 0; i < MAX_ANALYZER_STREAMS; i++)
    {
        if (g_analyzer_list[i].queue != NULL)
        {
            char analyzer_name[1024];
            snprintf(analyzer_name, sizeof(analyzer_name), "%s-%d", QUEUE_ANALYZER, i);
            for (int j = 0; j < MAX_WORKER_THREADS; j++)
            {
                if (pthread_create(&(g_analyzer_thread[n++]), NULL, lua_analyzer_thread, (void *)&g_analyzer_list[i]) != 0)
                {
                    syslog(LOG_ERR, "pthread_create() %s", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    /*
    * Create timer thread
    */
    if (pthread_create(&(g_analyzer_thread[n++]), NULL, lua_timer_thread, (void *)NULL) != 0)
    {
        syslog(LOG_ERR, "pthread_create() %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (g_drop_priv)
    {
        process_drop_privilege();
    }
    /*
     *
     */
    if (g_chroot)
    {
        if (chroot(dragonfly_analyzer_root) != 0)
        {
            syslog(LOG_ERR, "unable to chroot() to : %s - %s\n", dragonfly_analyzer_root, strerror(errno));
            exit(EXIT_FAILURE);
        }
        syslog(LOG_INFO, "chroot: %s\n", dragonfly_analyzer_root);
    }

    /* check to see that analyzer descripton files (json) exists. */
    for (int i = 0; g_analyzer_list[i].queue != NULL; i++)
    {
        struct stat sb;
        char json_file[PATH_MAX];
        snprintf(json_file, sizeof(json_file) - 1, "%s%s.json", WEB_DIR, g_analyzer_list[i].tag);

        if (lstat(json_file, &sb) < 0)
        {
            syslog(LOG_ERR, "analyzer description file %s does not exist.\n", json_file);
        }
    }

    /* start the static web interface to serve up analyzer explaination */
    void *web_ctx = start_web_server(WEB_DIR, WEB_PORT);

    while (g_running)
    {
        sleep(1);
    }

    stop_web_server(web_ctx);

    n = 0;
    while (g_analyzer_thread[n])
    {
        pthread_join(g_analyzer_thread[n++], NULL);
    }

    for (int i = 0; g_analyzer_list[i].queue != NULL; i++)
    {
        msgqueue_cancel(g_analyzer_list[i].queue);
    }

    for (int i = 0; g_analyzer_list[i].queue != NULL; i++)
    {
        msgqueue_destroy(g_analyzer_list[i].queue);
        g_analyzer_list[i].queue = NULL;
    }
    
    exit(EXIT_SUCCESS);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

static void create_message_queues()
{
    for (int i = 0; i < MAX_ANALYZER_STREAMS; i++)
    {
        if (g_analyzer_list[i].script != NULL)
        {
            char analyzer_name[1024];
            snprintf(analyzer_name, sizeof(analyzer_name), "%s-%d", QUEUE_ANALYZER, i);

            g_analyzer_list[i].queue = msgqueue_create(analyzer_name, _MAX_BUFFER_SIZE_, MAX_RING_BUFFER_SIZE);
        }
    }
    for (int i = 0; i < MAX_INPUT_STREAMS; i++)
    {
        if (g_input_list[i].uri != NULL)
        {
            for (int j = 0; j < MAX_WORKER_THREADS; j++)
            {
                char input_name[1024];
                snprintf(input_name, sizeof(input_name), "%s-%d", QUEUE_INPUT, i);
                g_input_list[i].queue = msgqueue_create(input_name, _MAX_BUFFER_SIZE_, MAX_RING_BUFFER_SIZE);
            }
        }
    }

    for (int i = 0; i < MAX_OUTPUT_STREAMS; i++)
    {
        if (g_output_list[i].uri != NULL)
        {
            char output_name[PATH_MAX];
            snprintf(output_name, sizeof(output_name), "%s-%d-%s", QUEUE_OUTPUT, i, g_output_list[i].tag);
            g_output_list[i].queue = msgqueue_create(output_name, _MAX_BUFFER_SIZE_, MAX_RING_BUFFER_SIZE);
        }
    }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

static void destroy_message_queues()
{
    for (int i = 0; g_input_list[i].queue != NULL; i++)
    {
        msgqueue_cancel(g_input_list[i].queue);
    }
    for (int i = 0; g_analyzer_list[i].queue != NULL; i++)
    {
        msgqueue_cancel(g_analyzer_list[i].queue);
    }
    for (int i = 0; g_output_list[i].queue != NULL; i++)
    {
        msgqueue_cancel(g_output_list[i].queue);
    }
    sleep(1);
    for (int i = 0; g_input_list[i].queue != NULL; i++)
    {
        msgqueue_destroy(g_input_list[i].queue);
        g_input_list[i].queue = NULL;
    }
    for (int i = 0; g_analyzer_list[i].queue != NULL; i++)
    {
        msgqueue_destroy(g_analyzer_list[i].queue);
        g_analyzer_list[i].queue = NULL;
    }
    for (int i = 0; g_output_list[i].queue != NULL; i++)
    {
        msgqueue_destroy(g_output_list[i].queue);
        g_output_list[i].queue = NULL;
    }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

void shutdown_threads()
{
    g_running = 0;
    kill(g_analyzer_pid, SIGTERM);

    int n = 0;
    while (g_io_thread[n])
    {
        pthread_join(g_io_thread[n++], NULL);
    }
    pthread_barrier_destroy(&g_flywheel_barrier);
    pthread_barrier_destroy(&g_output_barrier);
    int status;
    waitpid(-1, &status, 0);

    munmap(g_stats, sizeof(MLE_STATS));
    g_stats = NULL;
    free(g_redis_host);
    sleep(1);
    destroy_message_queues();
    destroy_configuration();
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

void startup_threads()
{
    if (!g_initialized)
    {
        syslog(LOG_ERR, "%s: cannot start without initializing configuration\n", __FUNCTION__);
        fprintf(stderr, "%s: cannot start without initializing configuration\n", __FUNCTION__);
        exit(EXIT_FAILURE);
    }
    g_running = 1;
    /*
     * Make sure analyzer is operating in default root directory
     */
    if (chdir(g_root_dir) != 0)
    {
        syslog(LOG_ERR, "unable to chdir() to  %s", g_root_dir);
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "root directory: %s\n", g_root_dir);
    syslog(LOG_INFO, "root directory: %s\n", g_root_dir);

    create_message_queues();
    /*
     * Initialize the timer list BEFORE forking
     */
    memset(&g_timer_list, 0, sizeof(g_timer_list));

    /* 
     * add internal logger for logging 
     */
    g_timer_list[0].tag = strdup(g_output_list[0].tag);
    g_timer_list[0].queue = g_output_list[0].queue;

    /* all the other analyzers */
    for (int i = 1; i < MAX_ANALYZER_STREAMS; i++)
    {
        if (g_analyzer_list[i].queue != NULL)
        {
            g_timer_list[i].tag = strdup(g_analyzer_list[i].tag);
            g_timer_list[i].queue = g_analyzer_list[i].queue;
        }
    }
    /*
     * Initialize memory-map to be share by two process for
     * maintaining stats
     */
    if ((g_stats = mmap(0, sizeof(4096), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
    {
        syslog(LOG_ERR, "unable to mmap() MLE_STATS");
        exit(EXIT_FAILURE);
    }
    memset(g_stats, 0, 4096);

    int n = 0;
    if ((g_analyzer_pid = fork()) < 0)
    {
        syslog(LOG_ERR, "fork() failed : %s\n", strerror(errno));
        fprintf(stderr, "fork() failed : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    else if (g_analyzer_pid == 0)
    {

        // child launch_analyzer_process
        launch_analyzer_process(g_analyzer_dir);
    }
    else
    {
        signal(SIGUSR1, signal_log_rotate);

        for (int i = 0; i < MAX_INPUT_STREAMS; i++)
        {
            if (g_input_list[i].queue != NULL)
            {
                for (int j = 0; j < MAX_WORKER_THREADS; j++)
                {
                    if (pthread_create(&(g_io_thread[n++]), NULL, lua_input_thread, (void *)&g_input_list[i]) != 0)
                    {
                        syslog(LOG_ERR, "pthread_create() %s", strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
        // make a copy
        memcpy(g_flywheel_list, g_input_list, sizeof(g_flywheel_list));
        g_num_flywheel_threads = g_num_input_threads;
        pthread_barrier_init(&g_flywheel_barrier, NULL, g_num_flywheel_threads + 1);
        for (int i = 0; i < MAX_INPUT_STREAMS; i++)
        {
            if (g_flywheel_list[i].queue != NULL)
            {
                if (pthread_create(&(g_io_thread[n++]), NULL, lua_flywheel_thread, (void *)&g_flywheel_list[i]) != 0)
                {
                    syslog(LOG_ERR, "pthread_create() %s", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
        }
        pthread_barrier_wait(&g_flywheel_barrier);

        pthread_barrier_init(&g_output_barrier, NULL, g_num_output_threads + 1);
        for (int i = 0; i < MAX_OUTPUT_STREAMS; i++)
        {
            if (g_output_list[i].queue != NULL)
            {
                /*ping
         * check that file exists with execute permissions
         */
                for (int j = 0; j < MAX_WORKER_THREADS; j++)
                {
                    if (pthread_create(&(g_io_thread[n++]), NULL, lua_output_thread, (void *)&g_output_list[i]) != 0)
                    {
                        syslog(LOG_ERR, "pthread_create() %s", strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
        pthread_barrier_wait(&g_output_barrier);

        if (0 && g_drop_priv)
        {
            process_drop_privilege();
        }
    }
    syslog(LOG_INFO, "%s: threads running\n", __FUNCTION__);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

static void launch_lua_threads()
{

    startup_threads();

    while (g_running)
    {
        //TODO: listen to REST API here
        //TODO: update stats here
        sleep(1);
    }
    shutdown_threads();
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

void dragonfly_mle_run(const char *rootdir, const char *logdir, const char *rundir)
{

    //signal(SIGPIPE, SIG_IGN);

    initialize_configuration(rootdir, logdir, rundir);

    signal(SIGABRT, signal_abort);
    signal(SIGTERM, signal_term);
    launch_lua_threads();
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void dragonfly_mle_break()
{
    g_running = 0;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
uint64_t dragonfly_mle_running()
{
    return g_running;
}

/*
 * ---------------------------------------------------------------------------------------
 */

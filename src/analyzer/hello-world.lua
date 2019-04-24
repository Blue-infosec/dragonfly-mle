-- ----------------------------------------------
-- Hello World with the MLE
--
-- This analyzer uses Redis to count the specific events types
-- and append the count to the json event.
--
-- See https://github.com/counterflow-ai/dragonfly-analyzers
-- for more analyzers
--
-- ----------------------------------------------

-- Additional Lua functions can be added if the files live within the
-- /usr/local/dragonfly-mle/analyzer folder
require 'analyzer/utils'

-- ----------------------------------------------
-- Setup is called once upon intialization of the MLE
-- ----------------------------------------------
function setup()
    conn = hiredis.connect()
    assert(conn:command("PING") == hiredis.status.PONG)
	print (">>>>>>>> Hello World! analyzer running")
end

-- ----------------------------------------------
-- Loop is called each time an event is routed to this analyzer
-- ----------------------------------------------
function loop(msg)
    local eve = msg
    local fields = {"event_type",}
    if not check_fields(eve, fields) then -- check_fields is included in utils.lua
        -- Log missing field to /var/log/dragonfly-mle/dragonfly-mle.log
        dragonfly.log_event('Missing field event_type')
        -- Pass the event along for further processing
        dragonfly.analyze_event(default_analyzer, msg)
        return
    end
    event_type = eve.event_type
    -- Update and access the count in Redis
    -- There are two ways to call Redis, 1) with conn:command("INCR", "count:"..event_type)
    -- and 2) as shown below, the command_line is added to the MLE to allow for long commands.
    redis_cmd = "INCR count:" .. event_type
    reply = conn:command_line(redis_cmd)

    -- Redis returns a table on an error, so check the status
    if type(reply) == 'table' and reply.name ~= 'OK' then
        -- Log the error to /var/log/dragonfly-mle/dragonfly-mle.log
        dragonfly.log_event('hello_world: '..cmd..' : '..reply.name)
        -- Pass the event along for further processing
        dragonfly.analyze_event(default_analyzer, msg)
        return
    end
    -- Successful replies contain the value
    count = reply
    -- Add a new field to the event type
    eve["event_count"] = count 
    -- Pass the event along to the next analyzer
	dragonfly.output_event (default_analyzer, eve)
end

function initialize ( )
   beirdobot.LogPrint("Starting LUA-burp");
   beirdobot.regexp_add( nil, "(?i)(\\s|^)burps?(\\s|\\.|$)", "burpback" );
end

function shutdown ( )
   beirdobot.LogPrint("Ending LUA-burp");
end

function burpback ( server, channel, who, msg, msgtype )
   beirdobot.LoggedActionMessage( server, channel, "burps back at " .. who );
end

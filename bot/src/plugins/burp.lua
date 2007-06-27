function initialize ( )
   beirdobot.LogPrint("Starting LUA-burp");
   beirdobot.regexp_add( nil, "(?i)(\\B|\\s|^)burps?(\\s|\\.|$)", "burpback" );
   beirdobot.regexp_add( nil, "(?i)(\\B|\\s|^)belch(es)?(\\s|\\.|$)", 
                         "belchback" );
   beirdobot.botCmd_add( "burp", "burpcmd", "burphelp" );
end

function shutdown ( )
   beirdobot.LogPrint("Ending LUA-burp");
end

function burpback ( server, channel, who, msg, msgtype )
   beirdobot.LoggedActionMessage( server, channel, "burps back at " .. who );
end

function belchback ( server, channel, who, msg, msgtype )
   beirdobot.LoggedActionMessage( server, channel, 
                                  "belches back at " .. who .. 
                                  " and shatters the windows" );
end

function burpcmd ( server, channel, who, msg )
   if channel == null then
      -- beirdobot.transmitMsg( server, TX_PRIVMSG, who, "Beeeeeeeelch!" );
      beirdobot.transmitMsg( server, 3, who, "Beeeeeeeelch!" );
   else
      beirdobot.LoggedActionMessage( server, channel, 
                                     "burps loudly and crudely!" );
   end
end

function burphelp ( )
   return( "Performs a burp" );
end

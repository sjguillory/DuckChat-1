# DuckChat
Compilation:
  I have provided a makefile which loads the necessary libraries for socket connections and UUID creation, and creates server and client 64-bit executables.
  All you need to do is run the following commands from the DuckChat Source directory:
  make clean /*clears existing executables and object code*/
  make all /*creates the target programs*/

Client Usage & Example:
  (Setup)
        ./client [IP of listening server] [Port # for server] [desired username]
        ./client 127.0.0.1 4444 Connor
        ./client localhost 4444 Connor
  Note that you must have an active server listening on the designated IP and port or the client connection will be refused.
  The port number designated must be able to recieve UDP messages over the firewall, unless both server and client are running on the localhost.
  
  (Chatting)
  Running the client sends a login message with the desired username to the specified server.
  By default, logins also send a join message to the Common channel.
  
  You can start chatting on the Common channel by simply typing a message (which cannot be a command).
  Any messages sent on the channels you are joined to will be sent to your terminal, including your own messages.  You can see the channel and username of the message just to the left of the message content.
  
  To remove yourself from the common channel use the /leave command, followed by the channel name you wish to leave. Ex:
      /leave Common /*removes the user from channel Common*/
  
  To join a new channel, creating it if it doesn't already exist, use the /join command. Ex:
      /join new_channel /*allows the user to send messages in channel "new_channel"*/
  
  Note that any chat messages you type will be sent to EVERY channel you are joined on.
  Use the /logout command or exit the client program and your username and channel affiliation will be removed from the server, which will propogate your logout message to any connected servers which may be trying to send messages to you.
  
  
Server Usage and Examples:
  There are two options to start a server network:
    
  1) You can edit and run the ./start_and_run.sh script which launches a capital H-shaped topology on localhost ports by default.  The shape of the network is important because only neighboring servers communicate with eachother, but forward messages from other neighbors intelligently.
    
  2) You can run the server executable directly and specify a hostname and port. Ex:
      ./server localhost 4000
  Note that if you are creating a network of servers this way, you need to specify the direct neighbor server IP and port at the same time. Ex:
      ./server localhost 4000 localhost 4001 
  Now these servers will forward joins, logins, leaves, say messages, and refresh messages to eachother when neccessary.
  However, you still need to start the server program on the specified IP and port:
      ./server localhost 4001 localhost 4000 
  Since the first server program specified port 4001 as a direct neighbor, the server on port 4001 will also be expected to specify the server on port 4000 as a direct neighbor.
  Note that if a server disconnects unexpectedly, direct neighbors will not recieve refresh messages from that server, and will remove any stored user and channel data and stop forwarding messages for those users and channels.  Once the server comes back online, users will have to login and rejoin their channels.
    
  The default period for refresh messages is 60 seconds.

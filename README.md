
<h1>DuckChat</h1>
<h2>Compilation:</h2>
  I have provided a makefile which loads the necessary libraries for socket connections and UUID creation, and creates server and client 64-bit executables.
  <br>All you need to do is run the following commands from the DuckChat Source directory:
  <br>make clean /*clears existing executables and object code*/
  <br>make all /*creates the target programs*/

<h2>Client Usage & Example:</h2>
  (Setup)
  <br>      ./client [IP of listening server] [Port # for server] [desired username]
  <br>      ./client 127.0.0.1 4444 Connor
  <br>      ./client localhost 4444 Connor
  Note that you must have an active server listening on the designated IP and port or the client connection will be refused.
  <br>The port number designated must be able to recieve UDP messages over the firewall, unless both server and client are running on the localhost.
  
  (Chatting)
  <br>Running the client sends a login message with the desired username to the specified server.
  <br>By default, logins also send a join message to the Common channel.
  
  <br>You can start chatting on the Common channel by simply typing a message (which cannot be a command).
  <br>Any messages sent on the channels you are joined to will be sent to your terminal, including your own messages.  You can see the channel and username of the message just to the left of the message content.
  
  <br>To remove yourself from the common channel use the /leave command, followed by the channel name you wish to leave. Ex:
  <br>    /leave Common /*removes the user from channel Common*/
  
  <br>To join a new channel, creating it if it doesn't already exist, use the /join command. Ex:
  <br>    /join new_channel /*allows the user to send messages in channel "new_channel"*/
  
  <br>Note that any chat messages you type will be sent to EVERY channel you are joined on.
  <br>Use the /logout command or exit the client program and your username and channel affiliation will be removed from the server, which will propogate your logout message to any connected servers which may be trying to send messages to you.
  
  
<h2>Server Usage and Examples:</h2>
  There are two options to start a server network:
    
  1) You can edit and run the ./start_and_run.sh script which launches a capital H-shaped topology on localhost ports by default.  The shape of the network is important because only neighboring servers communicate with eachother, but forward messages from other neighbors intelligently.
    
  2) You can run the server executable directly and specify a hostname and port. Ex:
  <br>    ./server localhost 4000
  
  <br>Note that if you are creating a network of servers this way, you need to specify the direct neighbor server IP and port at the same time. Ex:
  <br>    ./server localhost 4000 localhost 4001 
  
  <br>Now these servers will forward joins, logins, leaves, say messages, and refresh messages to eachother when neccessary.
  <br>However, you still need to start the server program on the specified IP and port:
  <br>    ./server localhost 4001 localhost 4000 
  <br>Since the first server program specified port 4001 as a direct neighbor, the server on port 4001 will also be expected to specify the server on port 4000 as a direct neighbor.
  
  <br>Note that if a server disconnects unexpectedly, direct neighbors will not recieve refresh messages from that server, and will remove any stored user and channel data and stop forwarding messages for those users and channels.  Once the server comes back online, users will have to login and rejoin their channels.
    
  <br>The default period for refresh messages is 60 seconds.

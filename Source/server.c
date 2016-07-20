#include <uuid/uuid.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <iostream>
#include <time.h>

using namespace std;
#include "duckchat.h"

#define MAX_CONNECTIONS 10
#define HOSTNAME_MAX 100
#define MAX_MESSAGE_LEN 65536
#define MAX_MESSAGES 65536

typedef map<string,struct sockaddr_in> channel_type; //<username, sockaddr_in of user>

int s; //socket for listening
struct sockaddr_in server;
map<string,struct sockaddr_in> usernames; //<username, sockaddr_in of user>
map<string,int> active_usernames; //0-inactive , 1-active
map<string,string> rev_usernames; //<ip+port in string, username>
map<string,channel_type> channels;

string this_server_name;//contains hostname and port for debugging
map<string,struct sockaddr_in> servers;//<ip+port in string, sockaddr_in of server>
map<string,int> channel_subscriptions;//<channel name, 0-inactive 1-active>
map<string, map<string, struct sockaddr_in> > server_channels;//<channel name, <server_name (ip+port), sockaddr_in of server> >, used to track which adjacent servers are subscribed to a channel
map<string, pair<string, time_t> > server_timers;//<server identifier, <channel name, timer> >
//map<string, int> uuids;//<uuid_t, status int 0-no duplicates 1-duplicate found>
char uuids[MAX_MESSAGES][37];
int timer_flag;

void handle_socket_input();
void handle_login_message(void *data, struct sockaddr_in sock);
void handle_logout_message(struct sockaddr_in sock);
void handle_join_message(void *data, struct sockaddr_in sock);
void handle_leave_message(void *data, struct sockaddr_in sock);
void handle_say_message(void *data, struct sockaddr_in sock);
void handle_list_message(struct sockaddr_in sock);
void handle_who_message(void *data, struct sockaddr_in sock);
void handle_keep_alive_message(struct sockaddr_in sock);
void handle_server_say_message(void *data, struct sockaddr_in sock);
void handle_server_leave_message(void *data, struct sockaddr_in sock);
void handle_server_join_message(void *data, struct sockaddr_in sock);
void send_error_message(struct sockaddr_in sock, string error_msg);
void broadcast_join_message(string origin_server_key, char* channel);


int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		printf("Usage: ./server domain_name port_num\n");
		exit(1);
	}
    
	char hostname[HOSTNAME_MAX];
	int port;
    	
	strcpy(hostname, argv[1]);
	port = atoi(argv[2]);
	    
	struct hostent     *he;
    if ((he = gethostbyname(hostname)) == NULL)
    {
        printf("Error resolving hostname\n");
        exit(1);
    }
    struct in_addr **addr_list;
    addr_list = (struct in_addr**) he->h_addr_list;
    char serv_name[HOSTNAME_MAX];
    strcat(serv_name, inet_ntoa(*addr_list[0]));
    strcat(serv_name, ".");
    strcat(serv_name, argv[2]);
    this_server_name = serv_name;

    //initialize UUID array
    for (int i = 0; i < MAX_MESSAGES; i++)
    {
        for (int j = 0; j < 37; j++)
        {
            uuids[i][j] = -1;
        }
    }

	//create default channel Common
	string default_channel = "Common";
	map<string,struct sockaddr_in> default_channel_users;
	channels[default_channel] = default_channel_users;
	channel_subscriptions[default_channel] = 1;
    
    map<string, struct sockaddr_in> server_names;
    if (argc > 3) {
        if (((argc - 3) % 2) != 0) {
            printf("Invalid number of arguments.  Usage: ./server domain_name port_num server1_domain server1_port server2_domain server2_port ... serverN_domain serverN_port\n");
        }
        for (int i = 0; i < argc - 3; i++) {
            if (i % 2 == 0) {
                char key[HOSTNAME_MAX+32] = {0};
                char serv_name[HOSTNAME_MAX+32] = {0};
                
                if ((he = gethostbyname(argv[i+3])) == NULL)
                {
                    printf("Error resolving hostname\n");
                    exit(1);
                }
                
                addr_list = (struct in_addr**) he->h_addr_list;
                strcat(key, inet_ntoa(*addr_list[0]));
                strcat(key, ".");
                strcat(key, argv[i+4]);
                string server_name = key;
                struct sockaddr_in server_addr;
                servers[server_name] = server_addr;
                servers[server_name].sin_family = AF_INET;
                servers[server_name].sin_port = htons(atoi(argv[i+4]));
                memcpy(&servers[key].sin_addr, he->h_addr_list[0], he->h_length);
                //add name and addr_in to map of subscribers
                struct sockaddr_in server_addr_cpy;
                server_names[server_name] = server_addr_cpy;
                server_names[server_name].sin_family = AF_INET;
                server_names[server_name].sin_port = htons(atoi(argv[i+4]));
                memcpy(&server_names[key].sin_addr, he->h_addr_list[0], he->h_length);
                //cout << "Adding server to Common channel: " << (int)ntohs(server_names[server_name].sin_port) << endl;
            }
            //add map of adjacent servers to server_channels map for the default channel
            server_channels[default_channel] = server_names;
        }
    }

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		perror ("socket() failed\n");
		exit(1);
	}
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	if ((he = gethostbyname(hostname)) == NULL) {
		printf("error resolving hostname..");
		exit(1);
	}
	memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);

	int err;
	err = bind(s, (struct sockaddr*)&server, sizeof server);
	if (err < 0)
	{
		perror("bind failed\n");
	}
    
    
    //set clock for the first time
    time_t start_time;
    time(&start_time);
    //cout << "start_time = " << start_time << endl;
    timer_flag = 0;
    while(1) //server runs for ever
	{
        time_t current_time;
        time(&current_time);
        double elapsed_time = (double)difftime(current_time,start_time);
        //cout << "elapsed_time = " <<  elapsed_time << endl;
        if (elapsed_time >= 10)
        {
            
            //cout << "seconds elapsed: " << elapsed_time << endl;
            if (timer_flag == 6)//every 6 timeouts, send a refresh
            {
                timer_flag = 0;
                //for each adjacent server
                map<string, struct sockaddr_in>::iterator server_iter;
                for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
                {
                    //send a join for each subscribed channel
                    
                    map<string, channel_type>::iterator channel_iter;
                    for (channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
                    {
                        struct server_request_join s2s_join;
                        ssize_t bytes;
                        void *send_data;
                        size_t len;
                        s2s_join.req_type = SERVER_REQ_JOIN;
                        strcpy(s2s_join.req_channel, channel_iter->first.c_str());
                        send_data = &s2s_join;
                        len = sizeof s2s_join;
                        bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&(server_iter->second), sizeof (server_iter->second));
                        if (bytes < 0)
                        {
                            perror("Message failed");
                        }
                        else
                        {
                            cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) << " "
                                << inet_ntoa((server_iter->second).sin_addr) << ":" 
                                << (int)ntohs((server_iter->second).sin_port)
                                << " send S2S Join " << channel_iter->first << " (refresh)" << endl;
                        }
                    }
                    
                    //check for timeouts (interpret as leave)
                    map<string, map<string, struct sockaddr_in> >::iterator server_channel_iter;
                    for (server_channel_iter = server_channels.begin(); server_channel_iter != server_channels.end(); server_channel_iter++)
                    {
                        string server_identifier;
                        char port_str[6];
                        int port;
                        port = (int)ntohs(server_iter->second.sin_port);
                        sprintf(port_str, "%d", port);
                        strcpy(port_str, port_str);
                        string ip = inet_ntoa((server_iter->second).sin_addr);
                        server_identifier = ip + "." + port_str;
                        
                        string channel = server_channel_iter->first;
                        map<string, pair<string, time_t> >::iterator timer_iter;
                        timer_iter = server_timers.find(server_identifier);
                        if (timer_iter == server_timers.end() )
                        {
                            //no timer found for this channel/server combo
                            //nothing to do
                            //cout << "No timer found for this server/channel pair, nothing to do" << endl;
                        }
                        else
                        {
                            //timer found, check > 120
                            time_t curr_time;
                            time(&curr_time);
                            double elapsed = (double)difftime(curr_time, (timer_iter->second).second);
                            if (elapsed >= 120)
                            {
                                cout << "Removing server " << server_identifier << " from channel " << channel  << " subscribers!" << endl;
                                map<string, struct sockaddr_in>::iterator find_iter;
                                find_iter = server_channels[channel].find(server_identifier);
                                if (find_iter != server_channels[channel].end())
                                {
                                    server_channels[channel].erase(find_iter);
                                    break;
                                }
                                //cout << "Couldn't find server's channel's timer." << endl;
                                //server_channels[channel].erase(server_identifier);
                            }
                        }
                    }
                }
            }
            else
            {
                timer_flag++;
                //check join timers
                
                map<string, struct sockaddr_in>::iterator server_iter;
                for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
                {
                    //this should only remove servers from server_channels map (map<channel, map<server_id string, server sockaddr_in> >)
                    map<string, map<string, struct sockaddr_in> >::iterator channel_iter;
                    for (channel_iter = server_channels.begin(); channel_iter != server_channels.end(); channel_iter++)
                    {
                        string server_identifier;
                        char port_str[6];
                        int port = (int)ntohs((server_iter->second).sin_port);
                        sprintf(port_str,"%d",port); 
                        
                        string ip = inet_ntoa((server_iter->second).sin_addr);
                        server_identifier = ip + "." + port_str;
                        
                        string channel = channel_iter->first;
                        map<string, pair<string, time_t> >::iterator timer_iter;
                        timer_iter = server_timers.find(server_identifier);
                        if (timer_iter == server_timers.end() )
                        {
                            //no timer found for this channel/server combo
                        }
                        else
                        {
                            //timer found, check > 120
                            time_t curr_time;
                            time(&curr_time);
                            double elapsed = (double)difftime(curr_time, (timer_iter->second).second);
                            if (elapsed > 120)
                            {
                                //remove channel from subscriptions
                                map<string, struct sockaddr_in>::iterator find_iter;
                                find_iter = server_channels[channel].find(server_identifier);
                                if (find_iter != server_channels[channel].end())
                                {
                                    server_channels[channel].erase(find_iter);
                                    //cout << "Removing server from channel subscribers!" << endl;
                                    break;
                                }
                                //cout << "Couldn't find server's channel's timer." << endl;
                                //server_channels[channel].erase(server_identifier);
                            }
                            //else {cout << "elapsed < 120 for this comparison:" << elapsed << endl;}
                        }
                    }
                }
            }
            
            
            time(&start_time);//reset timer
        
        }
		//use a file descriptor with a timer to handle timeouts
		int rc;
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(s, &fds);
		struct timeval max_timeout_interval;
        max_timeout_interval.tv_sec = 10;
        max_timeout_interval.tv_usec = 0;
		rc = select(s+1, &fds, NULL, NULL, &max_timeout_interval);
		if (rc < 0)
		{
			printf("error in select\n");
            getchar();
		}
        else if (rc == 0)
        {
            //printf("Timer went off\n");
            //nothing to do
        }
		else
		{
			int socket_data = 0;

			if (FD_ISSET(s,&fds))
			{
				//reading from socket
				handle_socket_input();
				socket_data = 1;
			}
        }
	}
	return 0;
}


void handle_socket_input()
{
	struct sockaddr_in recv_client;
	ssize_t bytes;
	void *data;
	size_t len;
	socklen_t fromlen;
	fromlen = sizeof(recv_client);
	char recv_text[MAX_MESSAGE_LEN];
	data = &recv_text;
	len = sizeof recv_text;

	bytes = recvfrom(s, data, len, 0, (struct sockaddr*)&recv_client, &fromlen);

	if (bytes < 0)
	{
		perror ("recvfrom failed\n");
	}
	else
	{
		//printf("received message\n");

		struct request* request_msg;
		request_msg = (struct request*)data;

		//printf("Message type:");
		request_t message_type = request_msg->req_type;

		//printf("%d\n", message_type);

		if (message_type == REQ_LOGIN)
		{
			handle_login_message(data, recv_client); //some methods would need recv_client
		}
		else if (message_type == REQ_LOGOUT)
		{
			handle_logout_message(recv_client);
		}
		else if (message_type == REQ_JOIN)
		{
			handle_join_message(data, recv_client);
		}
		else if (message_type == REQ_LEAVE)
		{
			handle_leave_message(data, recv_client);
		}
		else if (message_type == REQ_SAY)
		{
			handle_say_message(data, recv_client);
		}
		else if (message_type == REQ_LIST)
		{
			handle_list_message(recv_client);
		}
		else if (message_type == REQ_WHO)
		{
			handle_who_message(data, recv_client);
		}
        else if (message_type == SERVER_REQ_JOIN)
        {
            handle_server_join_message(data, recv_client);
        }
        else if (message_type == SERVER_REQ_LEAVE)
        {
            handle_server_leave_message(data, recv_client);
        }
        else if (message_type == SERVER_REQ_SAY)
        {
            handle_server_say_message(data, recv_client);
        }
		else
		{
			//send error message to client
			send_error_message(recv_client, "*Unknown command");
		}
	}
}


/*	check whether the user is in usernames
	    if yes check whether channel is in channels
	        if channel is there add user to the channel
	        if channel is not there add channel and add user to the channel 
*/
void handle_login_message(void *data, struct sockaddr_in sock)
{

	struct request_login* msg;
	msg = (struct request_login*)data;
	string username = msg->req_username;
	usernames[username]	= sock;
	active_usernames[username] = 1;
	//rev_usernames[sock] = username;
	//char *inet_ntoa(struct in_addr in);
	string ip = inet_ntoa(sock.sin_addr);
	//cout << "ip: " << ip <<endl;
	int port = sock.sin_port;
	//unsigned short short_port = sock.sin_port;
	//cout << "short port: " << short_port << endl;
	//cout << "port: " << port << endl;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;
	string key = ip + "." + port_str;
	//cout << "key: " << key <<endl;
	rev_usernames[key] = username;
	cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
        << " " 
        << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port) 
        << " recv Request Login " << username << endl;
}


//logout requires no S2S communication (leave handles this?)
void handle_logout_message(struct sockaddr_in sock)
{
	//construct the key using sockaddr_in
	string ip = inet_ntoa(sock.sin_addr);
	//cout << "ip: " << ip <<endl;
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	//cout << "port: " << port_str << endl;
	string key = ip + "." +port_str;
	//cout << "key: " << key <<endl;
	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	/*
    for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
    {
        cout << "key: " << iter->first << " username: " << iter->second << endl;
    }
	*/
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//send an error message saying not logged in
		send_error_message(sock, "Not logged in");
	}
	else
	{
		//cout << "key " << key << " found."<<endl;
		string username = rev_usernames[key];
		rev_usernames.erase(iter);
		
        //remove from usernames
		map<string,struct sockaddr_in>::iterator user_iter;
		user_iter = usernames.find(username);
		usernames.erase(user_iter);

		//remove from all the channels if found
		map<string,channel_type>::iterator channel_iter;
		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			//cout << "key: " << iter->first << " username: " << iter->second << endl;
			//channel_type current_channel = channel_iter->second;
			map<string,struct sockaddr_in>::iterator within_channel_iterator;
			within_channel_iterator = channel_iter->second.find(username);
			if (within_channel_iterator != channel_iter->second.end())
			{
				channel_iter->second.erase(within_channel_iterator);
			}
		}
		//remove entry from active usernames also
		//active_usernames[username] = 1;
		map<string,int>::iterator active_user_iter;
		active_user_iter = active_usernames.find(username);
		active_usernames.erase(active_user_iter);
		cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
            << " " << inet_ntoa(sock.sin_addr) 
            << ":" << (int)ntohs(sock.sin_port) 
            << "recv Request Logout " << username << endl;
	}
}


//when a user joins the channel, serverchecks to see if it is already subscribed to that channel
//if so, the server need not take any additional steps.
//if not, the server must attempt to locate other servers subscribed to that channel.
//      the server begins by sending a S2S-join message to all adjacent servers
//          servers receiving this message must handle it, see handle_server_join_message() below
//          adjacent servers either forward the message
//          or adjacent servers do not forward.
void handle_join_message(void *data, struct sockaddr_in sock)
{
	//get message fields
	struct request_join* msg;
	msg = (struct request_join*)data;
	string channel = msg->req_channel;
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;
	//check whether key is in rev_usernames
	map<string,string>::iterator iter;
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];
		map<string,channel_type>::iterator channel_iter;
		channel_iter = channels.find(channel);
		active_usernames[username] = 1;
		if (channel_iter == channels.end())
		{
			//channel not found
			map<string,struct sockaddr_in> new_channel_users;
			new_channel_users[username] = sock;
			channels[channel] = new_channel_users;
			//cout << "creating new channel and joining" << endl;
            channel_subscriptions[channel] = 1;
            char channel_buf[CHANNEL_MAX];
            strcpy(channel_buf, channel.c_str());
            char port_str[6];
            string domain_name = inet_ntoa(server.sin_addr);
            sprintf(port_str, "%d", (int)ntohs(server.sin_port));
            string origin_server_key = domain_name + "." + port_str;
    
            //add each adjacent server to the subscribed servers list
            map<string, struct sockaddr_in>::iterator server_iterator;
            map<string, struct sockaddr_in> server_names;
            int count = 0;
            for (server_iterator = servers.begin(); server_iterator != servers.end(); server_iterator++)
            {
                count++;
                server_names[server_iterator->first] = server_iterator->second;
            }
            //cout << "Added " << count << " servers to subscriptions for " << channel;
            server_channels[channel] = server_names;

            cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                << " " << inet_ntoa(sock.sin_addr) 
                << ":" << (int)ntohs(sock.sin_port) 
                << " recv Request Join " << username << " wants to join channel \"" << channel << "\"" << endl;
            broadcast_join_message(origin_server_key, channel_buf);
		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in>* existing_channel_users;
			//existing_channel_users = &channels[channel];
			//*existing_channel_users[username] = sock;
			channels[channel][username] = sock;
			//cout << "joining exisitng channel" << endl;
		}
	}
}


void handle_leave_message(void *data, struct sockaddr_in sock)
{
	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes, remove user from channel
	//if not send an error message to the user

	//get message fields
	struct request_leave* msg;
	msg = (struct request_leave*)data;
	string channel = msg->req_channel;
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;
	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];
		map<string,channel_type>::iterator channel_iter;
		channel_iter = channels.find(channel);
		active_usernames[username] = 1;
		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
		    cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                << " " << inet_ntoa(sock.sin_addr) 
                << ":" << (int)ntohs(sock.sin_port) 
                << "recv Request Leave " << username 
                << " trying to leave non-existent channel " << channel << endl;
		}
		else
		{
			//channel already exists
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);

			if (channel_user_iter == channels[channel].end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
                cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                    << " " << inet_ntoa(sock.sin_addr) 
                    << ":" << (int)ntohs(sock.sin_port) 
                    << "recv Request Leave " << username 
                    << " trying to leave channel " << channel << "where he/she is not a member" << endl;
			}
			else
			{
				channels[channel].erase(channel_user_iter);
				//existing_channel_users.erase(channel_user_iter);
                cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                    << " " << inet_ntoa(sock.sin_addr) 
                    << ":" << (int)ntohs(sock.sin_port) 
                    << "recv Request Leave " << username 
                    << " leaves channel " << channel << endl;
				//delete channel if no more users
				if (channels[channel].empty() && (channel != "Common"))
				{
					channels.erase(channel_iter);
                    channel_subscriptions.erase(channel);
                    cout << inet_ntoa(server.sin_addr) 
                        << ":" << (int)ntohs(server.sin_port) 
                        << " server: removing empty channel" << endl;
                    //Should we trim the tree? Lazy method is to trim when Say cannot be forwarded
                    
				}
			}
		}
	}
}


void handle_say_message(void *data, struct sockaddr_in sock)
{
	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//check whether the user is in the channel
	//if yes send the message to all the members of the channel
	//if not send an error message to the user

	//get message fields
	struct request_say* msg;
	msg = (struct request_say*)data;
	string channel = msg->req_channel;
	string text = msg->req_text;
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;
	
    //check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		map<string,channel_type>::iterator channel_iter;
		channel_iter = channels.find(channel);
		active_usernames[username] = 1;
		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
            cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                << " " << inet_ntoa(sock.sin_addr) 
                << ":" << (int)ntohs(sock.sin_port) 
                << " " << username 
                << " trying to send a message to non-existent channel " << channel << endl;
		}
		else
		{
			//channel already exits
			//map<string,struct sockaddr_in> existing_channel_users;
			//existing_channel_users = channels[channel];
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);
			if (channel_user_iter == channels[channel].end())
			{
				//user not in channel
				send_error_message(sock, "You are not in channel " + channel);
                cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                    << " " << inet_ntoa(sock.sin_addr) 
                    << ":" << (int)ntohs(sock.sin_port) 
                    << " " << username 
                    << " trying to send a message to channel " << channel << "where he/she is not a member" << endl;
			}
			else
			{
				map<string,struct sockaddr_in> existing_channel_users;
				existing_channel_users = channels[channel];
				for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
				{
					//cout << "key: " << iter->first << " username: " << iter->second << endl;

					ssize_t bytes;
					void *send_data;
					size_t len;
					struct text_say send_msg;
					send_msg.txt_type = TXT_SAY;
					const char* str = channel.c_str();
					strcpy(send_msg.txt_channel, str);
					str = username.c_str();
					strcpy(send_msg.txt_username, str);
					str = text.c_str();
					strcpy(send_msg.txt_text, str);
					//send_msg.txt_username, *username.c_str();
					//send_msg.txt_text,*text.c_str();
					send_data = &send_msg;
					len = sizeof send_msg;
					//cout << username <<endl;
					struct sockaddr_in send_sock = channel_user_iter->second;
					//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
					bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);
					if (bytes < 0)
					{
						perror("Message failed"); //error
					}
					else
					{
						//printf("Message sent\n");
					}
				}
            
                cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
                    << " " << inet_ntoa(sock.sin_addr) 
                    << ":" << (int)ntohs(sock.sin_port) 
                    << " Server: recv Request Say " << channel << " \"" << text << "\"" << endl;
            
            
                uuid_t uuid;
                uuid_generate(uuid);//uses /dev/urandom if available
                char uuid_chars[37];
                uuid_unparse(uuid, uuid_chars);
                //strcpy(s2s_say.uuid_str, uuid_chars);
                char* uuid_ptr = uuid_chars;
                //cout << "UUID at time of generation: " << uuid_chars << endl;
                //string uuid_str = uuid_ptr;
                //cout << "UUID_str after gen: " << uuid_str << endl;
                
                //insert a new UUID into UUID array
                for (int i = 0; i < MAX_MESSAGES; i++)
                {
                    if (uuids[i][0] == -1)
                    {
                        strcpy(uuids[i],uuid_chars);
                        break;
                    }
                }
                //cout << "inserted a new UUID." << endl;
                //uuids[uuid_str] = 0;//use string as key (remember to convert back to char[37])
                //for each existing server, send a server to server say request
                map<string, struct sockaddr_in> existing_channel_servers;
                existing_channel_servers = server_channels[channel];
                map<string, struct sockaddr_in>::iterator server_iter;
                for (server_iter = existing_channel_servers.begin(); server_iter != existing_channel_servers.end(); server_iter++)
                {
                    ssize_t bytes;
                    void *send_data;
                    size_t len;
                    struct server_request_say s2s_say;
                    s2s_say.req_type = SERVER_REQ_SAY;
                    const char* str = channel.c_str();
                    strcpy(s2s_say.req_channel, str);
                    str = username.c_str();
                    strcpy(s2s_say.req_username, str);
                    str = text.c_str();
                    strcpy(s2s_say.req_text, str);

                    //

                    for (int k = 0; k < MAX_MESSAGES; k++)
                    {
                        if (strcmp(uuids[k],uuid_chars) == 0)
                        {
                            strcpy(s2s_say.uuid_str, uuids[k]);//copy in UUID
                        }
                    }
                    send_data = &s2s_say;
                    len = sizeof s2s_say;
                    struct sockaddr_in send_sock = server_iter->second;
                    //cout << server_iter->first << endl;
                    //cout << "len: " << (int)len << "send_sock.sin_addr: " << inet_ntoa(send_sock.sin_addr) << ":" << (int)ntohs(send_sock.sin_port) << " " << (int)(sizeof send_sock) << endl;
                    bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);
                    if (bytes < 0)
                    {
                        perror("Message failed");//error
                    }
                    else
                    {
                    cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
                        << " " << inet_ntoa(send_sock.sin_addr)
                        << ":" << (int)ntohs(send_sock.sin_port)
                        << " send S2S Request Say " << channel << " " 
                        << username << " \"" << text << "\"" << endl;
                    }
                }
			}
		}
	}
}


void handle_list_message(struct sockaddr_in sock)
{
	//check whether the user is in usernames
	//if yes, send a list of channels
	//if not send an error message to the user

	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;

	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;

	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		int size = channels.size();
		//cout << "size: " << size << endl;
		active_usernames[username] = 1;
		ssize_t bytes;
		void *send_data;
		size_t len;
		//struct text_list temp;
		struct text_list *send_msg = (struct text_list*)malloc(sizeof (struct text_list) + (size * sizeof(struct channel_info)));
		send_msg->txt_type = TXT_LIST;
		send_msg->txt_nchannels = size;
		map<string,channel_type>::iterator channel_iter;

		//struct channel_info current_channels[size];
		//send_msg.txt_channels = new struct channel_info[size];
		int pos = 0;
		for(channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			string current_channel = channel_iter->first;
			const char* str = current_channel.c_str();
			//strcpy(current_channels[pos].ch_channel, str);
			//cout << "channel " << str <<endl;
			strcpy(((send_msg->txt_channels)+pos)->ch_channel, str);
			//strcpy(((send_msg->txt_channels)+pos)->ch_channel, "hello");
			//cout << ((send_msg->txt_channels)+pos)->ch_channel << endl;
			pos++;
		}
		//send_msg.txt_channels =
		//send_msg.txt_channels = current_channels;
		send_data = send_msg;
		len = sizeof (struct text_list) + (size * sizeof(struct channel_info));
		struct sockaddr_in send_sock = sock;
		bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);

		if (bytes < 0)
		{
			perror("Message failed\n"); //error
		}
		else
		{
			//printf("Message sent\n");
		}
        cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port) 
            << " " << inet_ntoa(sock.sin_addr) 
            << ":" << (int)ntohs(sock.sin_port) 
            << " recv Request List " << endl;
	}
}


void handle_who_message(void *data, struct sockaddr_in sock)
{
	//check whether the user is in usernames
	//if yes check whether channel is in channels
	//if yes, send user list in the channel
	//if not send an error message to the user

	//get message fields
	struct request_who* msg;
	msg = (struct request_who*)data;
	string channel = msg->req_channel;
	string ip = inet_ntoa(sock.sin_addr);
	int port = sock.sin_port;
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;
	//check whether key is in rev_usernames
	map <string,string> :: iterator iter;
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end() )
	{
		//ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		active_usernames[username] = 1;
		map<string,channel_type>::iterator channel_iter;
		channel_iter = channels.find(channel);
		if (channel_iter == channels.end())
		{
			//channel not found
			send_error_message(sock, "No channel by the name " + channel);
		    cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
               << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
               << "recv Request Who for non-existing channel " << channel << endl;
		}
		else
		{
			//channel exits
			map<string,struct sockaddr_in> existing_channel_users;
			existing_channel_users = channels[channel];
			int size = existing_channel_users.size();
			ssize_t bytes;
			void *send_data;
			size_t len;
			struct text_who *send_msg = (struct text_who*)malloc(sizeof (struct text_who) + (size * sizeof(struct user_info)));
			send_msg->txt_type = TXT_WHO;
			send_msg->txt_nusernames = size;
			const char* str = channel.c_str();
			strcpy(send_msg->txt_channel, str);
			map<string,struct sockaddr_in>::iterator channel_user_iter;
			int pos = 0;
			for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
			{
				string username = channel_user_iter->first;
				str = username.c_str();
				strcpy(((send_msg->txt_users)+pos)->us_username, str);
				pos++;
			}
			send_data = send_msg;
			len = sizeof(struct text_who) + (size * sizeof(struct user_info));
			struct sockaddr_in send_sock = sock;
			//bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
			bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);
			if (bytes < 0)
			{
				perror("Message failed\n"); //error
			}
			else
			{
				//printf("Message sent\n");
			}
            cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
                << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
                << "recv Request Who " << channel << endl;
			}
	}
}


void handle_server_join_message(void *data, struct sockaddr_in sock)
{
    //if this server is already subscribed to the provided channel, FIXME add all adj servers to subs
    //otherwise, subscribe this server, and send broadcast message to every adjacent server
	struct server_request_join* msg;
	msg = (struct server_request_join*)data;
	char channel[CHANNEL_MAX] = {0};
    strcpy(channel, msg->req_channel);
    //check whether channel is in channel_subscriptions
	map<string,int>::iterator subscription_iter;
	subscription_iter = channel_subscriptions.find(channel);
    char port_str[6];
    sprintf(port_str, "%d", htons(sock.sin_port));
    string domain_name = inet_ntoa(sock.sin_addr);
    string origin_server_key = domain_name + "." + port_str;
	if (subscription_iter == channel_subscriptions.end() )
	{
	    //channel not found, which means we must subscribe and broadcast
        channel_subscriptions[channel] = 1;
        cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
            << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
            << " recv S2S Join " << channel << " (forwarding)" << endl;
        
        map<string,struct sockaddr_in>::iterator server_iter;
        map<string,struct sockaddr_in> server_names;
        for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
        {
            
                server_names[server_iter->first] = server_iter->second;
            
            
            
                //this server_iter is the server we received the join from
                //if the timer for this server on this channel does not exist
                //  create it
                //otherwise
                //  reset it's channel timer to current time           
                map<string, pair<string, time_t> >::iterator timer_iter;
                timer_iter = server_timers.find(origin_server_key);
                if (timer_iter == server_timers.end())
                {
                    //timer not found
                    time_t new_time;
                    time(&new_time);
                    pair<string, time_t> channel_time_pair;
                    channel_time_pair.first = channel;
                    memcpy(&channel_time_pair.second, &new_time, sizeof new_time);
                    server_timers[origin_server_key] = channel_time_pair;
                }
                else
                {
                    //timer found, reset it.
                    time(&((timer_iter->second).second));
                }
                //end
            
        }
        server_channels[channel] = server_names;
        broadcast_join_message(origin_server_key, channel);
        
    }
    else
    {
        //channel found, reset the server's timer for this channel
        //search the map of timers on this channel for the timer
        map<string, struct sockaddr_in>::iterator server_iter;
        for (server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
        {
            if (origin_server_key == server_iter->first)
            {
                map<string, pair<string, time_t> >::iterator timer_iter;
                timer_iter = server_timers.find(origin_server_key);
                if (timer_iter == server_timers.end())
                {
                    //cout << "Timer for this server/channel pair was not found.  Creating it." << endl;
                    //timer not found
                    time_t new_time;
                    time(&new_time);
                    pair<string, time_t> channel_time_pair;
                    channel_time_pair.first = channel;
                    memcpy(&channel_time_pair.second,  &new_time, sizeof new_time);
                    server_timers[origin_server_key] = channel_time_pair;
                    (server_channels[channel])[server_iter->first] = server_iter->second;
                }
                else
                {
                    //cout << "Refreshing timer for server:" << origin_server_key << " at channel:" << channel << endl;
                    //timer found, reset it.
                    time(&((timer_iter->second).second));
                }
            }
            else
            {   
                //add this server as a subscriber to channel
                //(server_channels[channel])[server_iter->first] = server_iter->second;
            }
        }
        cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
            << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
            << " recv S2S Join " << channel << " (forwarding ends here)" << endl;
    }

}


//remove sending server from server_channels for the designated channel
void handle_server_leave_message(void *data, struct sockaddr_in sock)
{

	struct server_request_leave* msg;
	msg = (struct server_request_leave*)data;
	string channel = msg->req_channel;
	string ip = inet_ntoa(sock.sin_addr);
	int port = ntohs(sock.sin_port);
 	char port_str[6];
 	sprintf(port_str, "%d", port);
	string key = ip + "." +port_str;
	
    //remove server from server_channels subscription list/map
    //find server in map with key
    map<string,struct sockaddr_in>::iterator server_iter;
    
	server_iter = server_channels[channel].find(key);
	if (server_iter != server_channels[channel].end())
	{
		server_channels[channel].erase(server_iter);
    }		

    cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
        << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
        << " recv S2S Leave " << channel << endl;
}


//send a leave message back to sender if the sender is the only subscribed server and there are no clients
void handle_server_say_message(void *data, struct sockaddr_in sock)
{
	struct server_request_say* msg;
	msg = (struct server_request_say*)data;
	char channel[CHANNEL_MAX] = {0};
    char username[USERNAME_MAX] = {0};
    char text[SAY_MAX] = {0};
    strcpy(channel, msg->req_channel);
    strcpy(username, msg->req_username);
    strcpy(text, msg->req_text);
    char uuid_chars[37] = {0};
    strcpy(uuid_chars, msg->uuid_str);
    //string uuid_str = uuid_chars;
    //cout << "RECEIVED UUID as: " << uuid_chars << " channel: " << channel << " username: " << username << endl;

    //determine if the say message can be forwarded
    //  if so, forward it out all remaining interfaces
    //  if not, check if channel's client count is 0, then send a leave message back to sender of this msg
    string ip = inet_ntoa(sock.sin_addr);
    char port_str[6];
 	sprintf(port_str, "%d", (int)ntohs(sock.sin_port));
    string origin_server = ip + "." + port_str;


    cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
        << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(sock.sin_port)
        << " recv S2S Say(1) "<< username << " " << channel << " \"" << text << "\"" << endl;
    
    //if msg.uuid is in uuids, this is a duplicate.  do not forward, and should we send a leave?
	//map<string,int>::iterator iter;
    //iter = uuids.find(uuid_str);
	//if (iter == uuids.end() )
    for (int i = 0; i < MAX_MESSAGES; i++)
    {
        //if UUID not found in array
        if (uuids[i][0] == -1)
        {

            //not found, so add it to uuids
            //cout << "GUID not found (not a duplicate?)" << endl;
            strcpy(uuids[i], uuid_chars);
            //then send text_say to each subscribed user
            
            map<string,struct sockaddr_in>::iterator channel_user_iter;
            map<string,struct sockaddr_in> existing_channel_users;
            existing_channel_users = channels[channel];
            for(channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
            {
                //cout << "key: " << iter->first << " username: " << iter->second << endl;

                ssize_t bytes;
                void *send_data;
                size_t len;
                struct text_say send_msg;
                send_msg.txt_type = TXT_SAY;
                strcpy(send_msg.txt_channel, channel);
                strcpy(send_msg.txt_username, username);
                strcpy(send_msg.txt_text, text);
                send_data = &send_msg;
                len = sizeof send_msg;
                struct sockaddr_in send_sock = channel_user_iter->second;
                bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);
                if (bytes < 0)
                {
                    perror("Message failed"); //error
                }
            }
            break;
        }
        //else if UUID is found in array
        else if (strcmp(uuids[i], uuid_chars) == 0)
        {

            //found, so do not forward, and should we send a leave? 
            //Apparently, yes, this prevents cycles and redundancies in the tree.
            //cout << "Duplicate say message. Send a leave." << endl;

            map<string,struct sockaddr_in>::iterator server_iter;
            for (server_iter = server_channels[channel].begin(); server_iter != server_channels[channel].end(); server_iter++)
            {
                if (origin_server == server_iter->first) {
                    //send leave back to sender
                    ssize_t bytes;
                    size_t len;
                    void *send_data;
                    struct server_request_leave s2s_leave;
                    
                    s2s_leave.req_type = SERVER_REQ_LEAVE;
                    strcpy(s2s_leave.req_channel, channel);
                    len = sizeof s2s_leave;
                    bytes = sendto(s, send_data, len, 0, (struct sockaddr*)(&server_iter->second), sizeof(server_iter->second));
                    if (bytes < 0)
                    {
                        perror("Message failed");
                    }
                    else
                    {
                        cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
                            << " " << inet_ntoa((server_iter->second).sin_addr) << ":"
                            << (int)ntohs((server_iter->second).sin_port)
                            << " send S2S Leave(2) " << channel << endl;
                    }
                    break;
                }
           }
            return;
        }
        else
        {
            //cout << "UUID: " << uuid_chars << " did not match with UUID: " << uuids[i] << endl;
        }
    }//end for loop through UUID array

    int server_subscribers = 0;
    map<string,struct sockaddr_in>::iterator server_iter;
    for (server_iter = server_channels[channel].begin(); server_iter != server_channels[channel].end(); server_iter++)
    {
        if (origin_server != server_iter->first) {
            server_subscribers++;
        }
    }
    //cout << "server subs = " << server_subscribers << endl;
    if (server_subscribers > 0)
    {
        
        //forward message to each server
        ssize_t bytes;
        size_t len;
        void *send_data;
        struct server_request_say s2s_say;
        
        s2s_say.req_type = SERVER_REQ_SAY;
        strcpy(s2s_say.req_username, username);
        strcpy(s2s_say.req_channel, channel);
        strcpy(s2s_say.req_text, text);
        //find the uuid
        for (int k = 0; k < MAX_MESSAGES; k++)
        {
            if (strcmp(uuid_chars, uuids[k]) == 0) {
                strcpy(s2s_say.uuid_str, uuids[k]);
            }
        }

        send_data = &s2s_say;
        len = sizeof s2s_say;
        //forward to all  REMAINING interfaces (not sender)
        for (server_iter = server_channels[channel].begin(); server_iter != server_channels[channel].end(); server_iter++)
        {

            //if server is not sender
            if (origin_server != server_iter->first)
            {
                
                //cout << "origin: " << origin_server << " != " << server_iter->first << endl;
                bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&server_iter->second, sizeof server_iter->second);
                if (bytes < 0)
                {
                    perror("Message failed");
                }
                else
                {
                cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
                   << " " << inet_ntoa(sock.sin_addr) << ":" << (int)ntohs(server_iter->second.sin_port)
                   << " send S2S Request Say " << username << " " << channel << " \"" << text << "\"" << endl;
                }
            }
            else
            {
                //cout << "Skipping sender (not forwarding back to sender)" << endl;
            }
            //else skip
        }
    }
    else // server_subscribers = 0
    {
        if (channels[channel].empty() && (channel != "Common"))
        {
            //cout << "Channel is empty on this server" << endl;
            ssize_t bytes;
            size_t len;
            void *send_data;
            struct server_request_leave s2s_leave;
            s2s_leave.req_type = SERVER_REQ_LEAVE;
            strcpy(s2s_leave.req_channel, channel);
            send_data = &s2s_leave;
            len = sizeof s2s_leave;
            bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&sock, sizeof sock);
            if (bytes < 0)
            {
                perror("Message failed");
            }
            else
            {
            cout << inet_ntoa(server.sin_addr) << ":" << (int)htons(server.sin_port)
               << " " << inet_ntoa(sock.sin_addr) <<":"<< (int)htons(sock.sin_port)
               << " send S2S Leave(3) " << channel << endl;
            }
        }
    }    
}


void broadcast_join_message(string origin_server_key, char* channel)
{
    ssize_t bytes;
    void *send_data;
    size_t len;
    map<string,struct sockaddr_in>::iterator server_iter;
    struct server_request_join join_msg;
    join_msg.req_type = SERVER_REQ_JOIN;
    strcpy(join_msg.req_channel, channel);
    send_data = &join_msg;
    len = sizeof join_msg;
    server_iter = servers.begin();
    //if (server_iter == servers.end()) { cout << "no servers" << endl; }
    for(server_iter = servers.begin(); server_iter != servers.end(); server_iter++)
    {
        if (server_iter->first != origin_server_key)
        {
            //cout<< "compare "<< server_iter->first << " with " << origin_server_key << endl;
            bytes = sendto(s, send_data, len, 0, 
                    (struct sockaddr*)&server_iter->second, sizeof server_iter->second);
            if (bytes < 0)
            {
                perror("Message failed\n");
            }
            else
            {
                cout << inet_ntoa(server.sin_addr) << ":" << (int)ntohs(server.sin_port)
                    << " " << inet_ntoa(server_iter->second.sin_addr) << ":" 
                    << (int)ntohs(server_iter->second.sin_port) << " send S2S Request Join " << channel << endl;
            }
        }
        else 
        {
            //cout << "Not forwarding back to sender." << endl;
        }
    }
}


void send_error_message(struct sockaddr_in sock, string error_msg)
{
	ssize_t bytes;
	void *send_data;
	size_t len;

	struct text_error send_msg;
	send_msg.txt_type = TXT_ERROR;

	const char* str = error_msg.c_str();
	strcpy(send_msg.txt_error, str);

	send_data = &send_msg;

	len = sizeof send_msg;
	struct sockaddr_in send_sock = sock;
	bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, sizeof send_sock);
	if (bytes < 0)
	{
		perror("Message failed\n"); //error
	}
}


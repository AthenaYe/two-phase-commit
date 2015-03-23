//self defined
#include "server.h"
#include "socklib.h"
#include "2pc.h" 

using namespace std;

bool system_failure = false;

string char_to_st(char *st)
{
	stringstream ss;
	string s;
	ss<<st;
	ss>>s;
	return s;
}

bool ask_vote(int newsockfd, int msg_type, char *operation)
{
	char buffer[256];
	char reply_msg[256];
	bzero(buffer, sizeof(buffer));
	bzero(reply_msg, sizeof(buffer));
	int ret = send_msg(newsockfd, (char *)&msg_type, sizeof(int));
	if(ret == ERROR_SEND_FAIL)
	{
		perror("one sock pip broke down");
		return false;
	}
	else if(ret == ERROR_CLIENT_CLOSE)
	{
		perror("client has closed");
		system_failure = true;
		return false;
	}
	int len = strlen(operation);
	ret = send_msg(newsockfd, (char *)&len, sizeof(int));
	if(ret == ERROR_SEND_FAIL)
	{
		perror("one sock pip broke down");
		return false;
	}
	else if(ret == ERROR_CLIENT_CLOSE)
	{
		perror("client has closed");
		system_failure = true;
		return false;
	}
	ret = send_msg(newsockfd, operation, strlen(operation)); //?
	if(ret == ERROR_SEND_FAIL)
	{
		perror("one sock pip broke down");
		return false;
	}
	else if(ret == ERROR_CLIENT_CLOSE)
	{
		perror("client has closed");
		return false;
	}
	ret = recv(newsockfd, &reply_msg, sizeof(int), 0);
	if(ret == -1)	// recv timeout 
	{
		perror("recv timeout");
		return false;
	}
	else if(ret == 0)
	{
		perror("client disconnected");
		system_failure = true;
		return false;
	}
	int response_type = *(int *) reply_msg;
	printf("response %d\n", response_type);
	if(response_type == RPLY_YES)
		return true;
	else return false;
}

bool loop_ask(int msg_type, char *operation)
{
	map<hostname, fd>::iterator it = client_hostname_set.begin();
	for(; it != client_hostname_set.end(); it++)
	{
		int client_fd = it->second;
		cout<<"ask "<<it->first<<" "<<it->second<<endl;
		if(ask_vote(client_fd, msg_type, operation) ==false)
			return false;
	}
	return true;
}

void loop_terminate()
{
	map<hostname, fd>::iterator it = client_hostname_set.begin();
	for(; it != client_hostname_set.end(); it++)
	{
		int client_fd = it->second;
		int term = MSG_TERMINATE;
		send(client_fd, (char *)&term, sizeof(int), 0); 
	}

}

bool loop_commit()
{
	map<hostname, fd>::iterator it = client_hostname_set.begin();
	int type = MSG_COMMIT;
	for(; it != client_hostname_set.end(); it++)
	{
		if(system_failure)
			return false;
		int client_fd = it->second;
		int type = MSG_COMMIT;
		int ret = send_msg(client_fd, (char *)&type, sizeof(int));
		if(ret == ERROR_SEND_FAIL)
		{
			perror("one sock pip broke down");
			return false;
		}
		else if(ret == ERROR_CLIENT_CLOSE)
		{
			perror("client has closed");
			system_failure = true;
			return false;
		}
	}
	return true;
}

void loop(int msg_type)
{
	map<hostname, fd>::iterator it = client_hostname_set.begin();
	int type = msg_type;
	for(; it != client_hostname_set.end(); it++)
	{
		int client_fd = it->second;
		send_msg(client_fd, (char *)&type, sizeof(int));
	}
	return;
}

void two_phase_commit(char * filename)
{
	puts(filename);
	int ask = 0;
	FILE * pFile;
	char operation[1010];
	memset(operation, 0, sizeof(operation));
	pFile = fopen (filename, "r");
	// phase one: loop through the cohorts set to see if all of them is available
	while(fgets(operation, 1000, pFile) != NULL)
	{
		if(system_failure)
			break;
		if(loop_ask(ASK_COMMIT, operation))
		{
			puts("ask success, begin commit");
			if(!loop_commit())
			{
				puts("commit failed, begin roll back");
				loop(MSG_ROLLBACK);
			}
			else 
			{
				puts("commit suc,");
				loop(MSG_COMPLETE); 
			}
		}
		else
		{
			puts("ask failed, begin abort");
			loop(MSG_ABORT);
		}
	}
	loop_terminate();
	return;
}

int main(int argc, char *argv[])
{
	if(argc < 2)
	{
		puts("./exec filename");
		return 1;
	}
	int sockfd, option=1;
	char hostname[256];
	int clinum = 0;
	socklen_t clilen;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)(&option), sizeof(option));
	struct timeval timeout; 
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	//	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
	//				sizeof(timeout)) < 0)
	//	{
	//		perror("setsockopt failed\n");
	//		return 1;
	//	}
	if (sockfd < 0) {
		perror("ERROR opening socket");
		return 1;
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = 0;

	socklen_t sockaddr_len = (socklen_t)sizeof(struct sockaddr);
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sockaddr_len) < 0) {
		perror("ERROR on binding");
		return 1;
	}

	// Print Server Socket Info
	gethostname(hostname, sizeof(hostname));
	getsockname(sockfd, (struct sockaddr *)&serv_addr, &sockaddr_len);
	printf("export SERVER_ADDRESS=%s\n", hostname);
	printf("export SERVER_PORT=%d\n", ntohs(serv_addr.sin_port));
	listen(sockfd, 5);
	while(true)
	{
		clilen = sizeof(cli_addr);
		int newsockfd = accept(sockfd, 
				(struct sockaddr *) &cli_addr, 
				&clilen);
		if (newsockfd < 0) {
			perror("ERROR on accept");
			return 1;
		}
		char client_host[NI_MAXHOST];
		getnameinfo((struct sockaddr *)&cli_addr, sizeof(cli_addr), client_host, sizeof(client_host), NULL, 0, NI_NUMERICHOST);
		printf("new comer: %s\n", client_host);

		string cli_addr_st = char_to_st(client_host);
		clinum++;
		client_hostname_set[cli_addr_st] = newsockfd;
		client_fd_set[newsockfd] = cli_addr_st;
		if(clinum == NUM_CLIENTS)
			break;
	}
	two_phase_commit(argv[1]);
	return 0;
}
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *get_content_type(const char* path) {
	const char *last_dot = strrchr(path, '.');
	if (last_dot) {
		if (strcmp(last_dot, ".css") == 0) return "text/css";
		if (strcmp(last_dot, ".csv") == 0) return "text/csv";
                if (strcmp(last_dot, ".gif") == 0) return "image/gif";
		if (strcmp(last_dot, ".htm") == 0) return "text/htm";
		if (strcmp(last_dot, ".html") == 0) return "text/html";
		if (strcmp(last_dot, ".ico") == 0) return "image/x-icon";
		if (strcmp(last_dot, ".jpeg") == 0) return "image/jpeg";
                if (strcmp(last_dot, ".jpg") == 0) return "image/jpeg";
		if (strcmp(last_dot, ".js") == 0) return "application/javascript";
		if (strcmp(last_dot, ".json") == 0) return "application/json";
		if (strcmp(last_dot, ".png") == 0) return "image/png";
		if (strcmp(last_dot, ".pdf") == 0) return "application/pdf";
		if (strcmp(last_dot, ".svg") == 0) return "image/svg+xml";
		if (strcmp(last_dot, ".txt") == 0) return "text/plain";
	}
	return "text/plain";
}

int create_socket(const char* host, const char* port) {
	printf("Configuring local address...\n");
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	struct addrinfo *bind_address;
	getaddrinfo(host, port, &hints, &bind_address);

	printf("Creating socket...\n");
	int socket_listen;
	socket_listen = socket(bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol);
	if (socket_listen < 0) {
		fprintf(stderr, "socket() failed. (%d)\n", errno);
		exit(1);
	}
	printf("Binding socket to local address...\n");
	if (bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen)) {
		fprintf(stderr, "bind() failed. (%d)\n", errno);
		exit(1);
	}
	freeaddrinfo(bind_address);
	printf("Listening...\n");
	if (listen(socket_listen, 10) < 0) {
		fprintf(stderr, "listen() failed. (%d)\n", errno);
		exit(1);
	}
	return socket_listen;
}

#define MAX_REQUEST_SIZE 2047
struct client_info {
	socklen_t address_length;
	struct sockaddr_storage address;
	int socket;
	char request[MAX_REQUEST_SIZE + 1];
	int received;
	struct client_info *next;
};
static struct client_info *clients = 0;

struct client_info *get_client(int s) {
	struct client_info *ci = clients;
	while(ci){
		if (ci->socket == s)
			break;
		ci = ci->next;
	}
	if (ci) return ci;

	struct client_info *n = (struct client_info*) calloc(1, sizeof(struct client_info));

	if(!n) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}
	
	n->address_length = sizeof(n->address);
	n->next = clients;
	clients = n;
	return n;
}

void drop_client(struct client_info *client) {
	close(client->socket);
	struct client_info **p = &clients;
	while(*p) {
		if (*p == client) {
			*p = client->next;
			free(client);
			return;
		}
		p = &(*p)->next;
	}
	fprintf(stderr, "drop_client not found.\n");
	exit(1);
}

const char* get_client_address(struct client_info *ci) {
	static char address_buffer[100];
	getnameinfo((struct sockaddr*)&ci->address, ci->address_length, address_buffer, sizeof(address_buffer), 0, 0, NI_NUMERICHOST);
	return address_buffer;
}

fd_set wait_on_clients(int server) {
	fd_set reads;
	FD_ZERO(&reads);
	FD_SET(server, &reads);
	int max_socket = server;
	struct client_info *ci = clients;
	while(ci) {
		FD_SET(ci->socket, &reads);
		if (ci->socket > max_socket) max_socket = ci->socket;
		ci = ci->next;
	}
	if (select(max_socket+1, &reads, 0, 0, 0) < 0) {
		fprintf(stderr, "select() failed. (%d)\n", errno);
		exit(1);
	}
	return reads;
}

void send_400(struct client_info *client) {
	const char*c400 = "HTTP/1.1 400 Bad Request\r\n"
		"Connection: close\r\n"
		"Content-Length: 11\r\n\r\nBad Request";
	send(client->socket, c400, strlen(c400), 0);

	drop_client(client);
}

void send_404(struct client_info *client) {
	const char* c404= "HTTP/1.1 404 Not Found\r\n"
		"Connection: close\r\n"
		"Content-Length: 9\r\n\r\nNot Found";
	send(client->socket, c404, strlen(c404), 0);

	drop_client(client);
}

void serve_resource(struct client_info *client, const char *path, const char* cookie) {
	printf("serve_resource %s %s\n", get_client_address(client), path);

	if (strcmp(path, "/") == 0) path="/index.html";
	if (strlen(path) > 100) { send_400(client); return; }
	if (strstr(path, "..")) { send_404(client); return; }
	char full_path[128];
	
	sprintf(full_path, "cookies/%s", path);
	FILE *fp = fopen(full_path, "rt");
	if (fp == NULL) {
		send_404(client); return; 
	}
	fseek(fp, 0L, SEEK_END);
	size_t cl = ftell(fp);
	rewind(fp);

	const char* ct = get_content_type(full_path);

	#define BSIZE 1024
	char buffer[BSIZE];

	sprintf(buffer, "HTTP/1.1 200 OK\r\n");
	send(client->socket, buffer, strlen(buffer), 0);

	sprintf(buffer, "Connection: close\r\n");
	send(client->socket, buffer, strlen(buffer), 0);

	sprintf(buffer, "Content-Length: %lu\r\n", cl);
	send(client->socket, buffer, strlen(buffer), 0);

	sprintf(buffer, "Content-Type: %s\r\n", ct);
	send(client->socket, buffer, strlen(buffer), 0);

	if (cookie) {
		sprintf(buffer, "Set-Cookie: id=%s; Max-Age=86400\r\n", cookie);
		send(client->socket, buffer, strlen(buffer), 0);
	}

	sprintf(buffer, "\r\n");
	send(client->socket, buffer, strlen(buffer), 0);

	int r = fread(buffer, 1, BSIZE, fp);
	while (r) {
		send(client->socket, buffer, r, 0);
		r = fread(buffer, 1, BSIZE, fp);
	}
	fclose(fp);
	drop_client(client);
}

FILE* open_cookie(char* id, char* option) {
	char full_path[128];
	sprintf(full_path, "cookies/%s", id);
	FILE* fp = fopen(full_path, option);

	if(!fp) {
		fprintf(stderr, "cookie file error\n");
		return 0;
	}
	
	printf("created/write on cookies/%s file\n", id);
	return fp;
}

int main() {
	int server = create_socket("127.0.0.1", "8080");
	int cookies = 0; //cookie(int)
	while(1) {
		fd_set reads;
		reads = wait_on_clients(server);
		
		if (FD_ISSET(server, &reads)) { //listening socket
			struct client_info *client = get_client(-1); //새로운 사용자 정보 저장
			client->socket = accept(server, (struct sockaddr*) &(client->address), &(client->address_length));
			if (client->socket < 0) {
				fprintf(stderr, "accept() failed. (%d)\n", errno);
				return 1;
			}
			printf("New Connection from %s.\n", get_client_address(client));
		}

		struct client_info *client = clients;
		while(client) {
			struct client_info *next = client->next;
			if (FD_ISSET(client->socket, &reads)) { //connected socket
				if (MAX_REQUEST_SIZE == client->received) {
					send_400(client);
					continue;
				}
				int r = recv(client->socket, client->request + client->received,
						MAX_REQUEST_SIZE - client->received, 0);
				if (r < 1) {
					printf("Unexpected disconnect from %s.\n", get_client_address(client));
					drop_client(client);
				} else {
					client->received += r;
					client->request[client->received] = 0;
					char *q = strstr(client->request, "\r\n\r\n");
					if (q) {
						if (strncmp("GET /", client->request, 5) && strncmp("POST /", client->request, 5)) { 
								//not GET && not POST
								send_400(client); 
						}else if (!strncmp("GET /", client->request, 5)) { //received GET
							char* cookie = strstr(client->request, "Cookie: id=");
							if (cookie) { //With Cookie
								cookie += 11;
								char *end_cookie = strstr(cookie, "\r");
								if(!end_cookie) { send_400(client); }
								else {
									*end_cookie = 0;
									serve_resource(client, cookie, 0);	
								}

							} else{ //Without cookie
								printf("%s", client->request);
								//cookie value
								char cookieid[8];
								sprintf(cookieid, "%d", cookies++); //cookie(toString)
								//create cookie file
								FILE* fp = open_cookie(cookieid, "w");
								if (fp) {
									printf("made cookiefile\n");
									fclose(fp);
								}
								serve_resource(client, cookieid, cookieid); 
							}
						}else if (!strncmp("POST /", client->request, 5)) { //received POST
							printf("\n%s", client->request);
							//content length
							char* cl = strstr(client->request, "Content-Length: ");
							cl += 16;
							int contentlength = strtol(cl, 0, 10);
							printf("size of bodyLength %d\n", contentlength);
							
							//print body
							char* bodystart = strstr(client->request, "\r\n\r\n");
							bodystart += 4;
							char* bodyend = bodystart + contentlength;
						        *bodyend = 0;
							printf("post body: %s\n\n", bodystart);
							
							char* cookie = strstr(client->request, "Cookie: id=");
							if (cookie) { //With Cookie
								cookie += 11;
								char *end_cookie = strstr(cookie, "\r");
								if(!end_cookie) { send_400(client); }
								else {
									*end_cookie = 0;
									printf("Cookie: id=%s\n", cookie);
									FILE* fp = open_cookie(cookie, "a");
									if (!fp) { //cookie file open fail
										printf("there is no id=%s cookie file", cookie);
									} else if (fputs(bodystart, fp) < 0) { //cookie file write fail
										printf("cookie write file fail");
									} 
									fclose(fp);
								}
							} else { //Without Cookie
								printf("%s", client->request);
								//cookie value
								char cookieid[8];
								sprintf(cookieid, "%d", cookies++); //cookie(toString)
								//create cookie file
								FILE* fp = open_cookie(cookieid, "w");
								if (fp) {
									printf("made cookiefile\n");
									fclose(fp);
								}
								
								//put body data in cookie file
								fp = open_cookie(cookieid, "a");
								if (!fp) {
									printf("there is no id=%s cookie file", cookieid);
								} else if (fputs(bodystart, fp) < 0) {
									printf("cooke write file fail");
								}
								fclose(fp);

								serve_resource(client, cookieid, cookieid);
							}
							       	
						}
					}
				}
			}
			client = next;
		}
	}
	printf("\nClosing socket...\n");
	close(server);

	printf("Finished.\n");
	return 0;
}

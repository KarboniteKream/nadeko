#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
// TODO: Use stdint.h.

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>

#define ANSI_RESET   "\x1b[0m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_YELLOW  "\x1b[33m"

void handle_signals(int signo)
{
	int status;

	if(signo == SIGCHLD)
	{
		pid_t pid;

		while((pid = waitpid(-1, &status, WNOHANG)) > 0)
		{
			printf("KILLED %d\n", pid);
			fflush(stdout);
		}

		// signal(SIGCHLD, handle_signals);
	}
}

int main(int argc, char **argv)
{
	struct sigaction act;
	act.sa_handler = handle_signals;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGCHLD, &act, NULL);

	// signal(SIGCHLD, handle_signals);

	// TODO: Use setvbuf() to force flush.
	printf(ANSI_CYAN "[INFO]" ANSI_RESET " Nadeko v0.0.1, pid=%d\n", getpid());
	fflush(stdout);

	int server_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	int port = argc > 1 ? atoi(argv[1]) : 1107;

	struct sockaddr_in sock_addr =
	{
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = htonl(INADDR_ANY)
	};

	// TODO: SO_REUSEADDR.
	if(bind(server_sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
	{
		printf(ANSI_RED "[ERR]" ANSI_RESET " Unable to bind the socket: %s.\n", strerror(errno));
		return 1;
	}

	// TODO: Handle error.
	listen(server_sock, 16);
	printf(ANSI_CYAN "[INFO]" ANSI_RESET " Listening on port %d.\n\n", port);
	fflush(stdout);

	int client_sock = 0, num_bytes = 0;
	struct sockaddr client_addr;
	unsigned int client_size = sizeof(client_addr);

	char request[1024];
	char response[1048576];
	char date[48];
	char modification_date[48];
	char filename[256];
	char content_type[16];

	while(true)
	{
		if((client_sock = accept(server_sock, &client_addr, &client_size)) == -1)
		{
			continue;
		}

		// TODO: Check for error.
		printf("FORK\n");
		pid_t cpid = fork();

		if(cpid > 0)
		{
			printf("forked to %d from %d\n", cpid, getpid());
			fflush(stdout);
			close(client_sock);
			continue;
		}

		// NOTE: If request is 0 bytes, the fork still happens.
		if((num_bytes = recv(client_sock, &request, sizeof(request) - 1, 0)) > 0)
		{
			request[num_bytes] = '\0';
			printf("%d " ANSI_YELLOW "<<<" ANSI_RESET " Received %d bytes:\n%s\n", getpid(), num_bytes, request);
			fflush(stdout);

			char *request_continue = strchr(request, '\n');
			long pos_end = request_continue - request;
			request[pos_end - 10] = '\0';
			request[pos_end] = '\0';

			if(strcmp(&request[pos_end] - 9, "HTTP/1.1\r") != 0)
			{
				printf(ANSI_RED "[ERR]" ANSI_RESET " Not a HTTP/1.1 request.");
				fflush(stdout);
				close(client_sock);
				continue;
			}

			long pos_filename = strchr(request, ' ') - request;
			request[pos_filename] = '\0';

			if(strcmp(request, "GET") != 0)
			{
				printf(ANSI_RED "[ERR]" ANSI_RESET " Not a GET request.");
				fflush(stdout);
				close(client_sock);
				continue;
			}

			// TODO: Handle parameters.
			char *path = &request[pos_filename + 1];

			if(strcmp(path, "/") == 0)
			{
				strcpy(filename, "index.html");
			}
			else
			{
				strncpy(filename, path + 1, 256);

				if(filename[strlen(filename) - 1] == '/')
				{
					strncat(filename, "index.html", 256);
				}
				else
				{
					char *slash = strrchr(filename, '/');

					// FIXME: Don't remove slash from path.
					if(strchr(slash == NULL ? filename : slash, '.') == NULL)
					{
						strncat(filename, "/index.html", 256);
					}
				}
			}

			time_t raw_time = time(NULL);
			struct tm *time_info = gmtime(&raw_time);
			strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", time_info);

			FILE *file = fopen(filename, "rb");
			struct stat file_stat;
			size_t header_size = 0, content_size = 0;

			if(file != NULL)
			{
				fseek(file, 0, SEEK_END);
				long file_size = ftell(file);
				fseek(file, 0, SEEK_SET);

				char *content = (char *)malloc((file_size + 1) * sizeof(char));
				fread(content, file_size, 1, file);

				char *file_type = strrchr(filename, '.') + 1;

				// TODO: strncmp().
				if(strcmp(file_type, "html") == 0 || strcmp(file_type, "css") == 0)
				{
					strncpy(content_type, "text", 16);
				}
				else if(strcmp(file_type, "png") == 0 || strcmp(file_type, "jpg") == 0 || strcmp(file_type, "ico") == 0)
				{
					strncpy(content_type, "image", 16);
				}

				stat(filename, &file_stat);
				time_info = gmtime(&file_stat.st_mtime);
				strftime(modification_date, sizeof(modification_date), "%a, %d %b %Y %H:%M:%S %Z", time_info);

				// TODO: Move.
				char *modified_since = strstr(request_continue + 1, "If-Modified-Since");

				if(modified_since != NULL)
				{
					modified_since += 19;
					modified_since[strchr(modified_since, '\r') - modified_since] = '\0';
				}

				// FIXME: Compare time_t.
				if(modified_since != NULL && strcmp(modified_since, modification_date) == 0)
				{
					header_size = snprintf(response, sizeof(response), "HTTP/1.1 304 Not Modified\r\nServer: nadeko/0.0.1\r\nDate: %s\r\nLast-Modified: %s\r\nConnection: close\r\n", date, modification_date);
				}
				else
				{
					header_size = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nServer: nadeko/0.0.1\r\nDate: %s\r\nContent-Type: %s/%s; charset=utf-8\r\nX-Content-Type-Options: nosniff\r\nContent-Length: %ld\r\nLast-Modified: %s\r\nConnection: close\r\n\r\n", date, content_type, file_type, file_size, modification_date);
					memcpy(response + header_size, content, file_size);
					content_size = file_size;
					response[header_size + content_size] = '\0';
				}

				free(content);
			}
			else
			{
				header_size = snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\nServer: nadeko/0.0.1\r\nDate: %s\r\n", date);
			}

			num_bytes = send(client_sock, &response, header_size + content_size, 0);
			response[header_size] = '\0';
			printf("%d " ANSI_BLUE ">>>" ANSI_RESET " Sent %d bytes:\n%s\n", getpid(), num_bytes, response);
			fflush(stdout);
		}
		else
		{
			printf("EMPTY REQUEST\n");
		}

		close(client_sock);
		break;
	}

	if(close(server_sock) == -1)
	{
		printf(ANSI_RED "[ERR]" ANSI_RESET " Unable to close the socket: %s.\n", strerror(errno));
		return 1;
	}

	printf("END %d\n", getpid());
	fflush(stdout);

	return 0;
}

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "libnmap.h"

/************************************************************************************/
/*				 Loads pull of ip addresses from the selected file					*/
/************************************************************************************/
char *read_ip_pull(const char *file) {

	int size = 0;
	int offset = 0;
	char *result = NULL;
	FILE *fd = fopen(file, "r");

	if (fd) {
		/* getting size of file */
		fseek(fd, 0, SEEK_END);
		size = ftell(fd);

		if (!size)
			return NULL;

		fseek(fd, 0, SEEK_SET);

		/* allocating the resources */
		result = (char *)malloc(size);
		if (result) {

			while (fgets(result + offset, MAX_DIAPASONE_LEN, fd)) {
				offset = strlen(result);
			}
		}

		fclose(fd);
		return result;

	} else {
		ERR_PRINT("An error has been occurred while opening the '%s'\n", file);
		return NULL;
	}
}

char *read_filter(const char *file) {

	int size = 0;
	int offset = 0;
	char *result = NULL;
	FILE *fd = fopen(file, "r");

	if (fd) {
		/* getting size of file */
		fseek(fd, 0, SEEK_END);
		size = ftell(fd);

		if (!size)
			return NULL;

		fseek(fd, 0, SEEK_SET);

		/* allocating the resources */
		result = (char *)malloc(size);
		if (result) {

			while (fgets(result + offset, MAX_FILTER_LEN, fd)) {
				offset = strlen(result);
			}
		}

		fclose(fd);
		return result;

	} else {
		ERR_PRINT("An error has been occurred while opening the '%s'\n", file);
		return NULL;
	}
}

/************************************************************************************/
/*							Releases pull of ip addresses							*/
/************************************************************************************/
void release_ip_pull(char *pull) {
	free(pull);
}

/************************************************************************************/
/*							Releases allocated filter								*/
/************************************************************************************/
void release_filter(char *filter) {
	free(filter);
}

/************************************************************************************/
/* Parses all the strings of ip addresses in pull and starts 'nmap' process for them  */
/************************************************************************************/
int go_parse_string(char *pFile, char *filter_only, char *filter_rej) {

	char *pch = NULL;
	char *string = NULL;

	pch = strtok(pFile,"\n");
	while (pch) {
		printf("Checking ip diapasone: %s with nmap..\n", pch);
		nmap_start(pch, "80,8080,8000", filter_only, filter_rej);	/* port list by default */
		pch = strtok(NULL, "\n");
	}
	return SUCCESS;
}

/************************************************************************************/
/*				Starts nmap process with selected IP address and port				*/
/************************************************************************************/
int nmap_start(const char *ipaddr, const char *port_list, char *filter_only, char *filter_rej) {

	char buf[1024]				= {0};
	char nmap_param_list[255]	= {0};

	FILE *fd					= NULL;
	
	snprintf(nmap_param_list, sizeof(nmap_param_list), "nmap -p %s --open -oG - %s", port_list, ipaddr);
	fd = popen(nmap_param_list, "r");
	if (fd) {
		while (fgets(buf, sizeof(buf), fd)) {
			if (strstr(buf, "open/")) {			/* if nmap's output contains "open/" byte sequence, we should process detailed parsing*/
				nmap_get_ip_and_port(buf, filter_only, filter_rej);
			}
		}
	} else {
		return ERROR;
	}

	pclose(fd);

	return SUCCESS;
}

/************************************************************************************/
/*				 Parsing of ip address and port from incoming buffer				*/
/************************************************************************************/
/*							PLEASE PAY YOUR ATTENTION!								*/
/* The code below is Linux dependent parsing code for nmap utility (6.40 version)	*/
/* Here I assume that the version dependent output will be remain in the same format*/
/* The output of nmap command I use is following:									*/
/*----------------------------------------------------------------------------------*/
/* Host: 109.108.72.75 (109-108-72-75.kievnet.com.ua)	Ports: 80/open/tcp//http///	Ignored State: filtered (2) */
/* Host: 109.108.72.20 (109-108-72-20.kievnet.com.ua)	Ports: 80/open/tcp//http///, 443/open/tcp//https///, 3389/open/tcp//ms-wbt-server///	Ignored State: filtered (7) */
/************************************************************************************/

int nmap_get_ip_and_port(char *buf, char *filter_only, char *filter_rej) {

	char ip_to_check[20]		= {0};
	char port_to_check[20]		= {0};

	if (strchr(buf, '(')) {		/* Just in case */
		/* obtaining the ip address */
		int ip_length = strchr(buf, '(') - (buf + 6) - 1;
		memcpy(ip_to_check, buf + 6, ip_length);
		ip_to_check[ip_length] = 0;

		/********************************************************************************/
		/* Obtaining the ports															*/
		/* Please note, that we can have multiple port values for one IP address		*/
		/* Such we should check them all												*/
		/********************************************************************************/

		/* getting the first port value */
		char *pch			= NULL;
		char *_ptr			= strstr(buf, "Ports:");
		int port_length		= 0;

		_ptr += strlen("Ports: ");
		port_length = strchr(_ptr, '/') - _ptr;
		memcpy(port_to_check, _ptr, port_length);
		port_to_check[port_length] = 0;

		nmap_web_server_check(ip_to_check, port_to_check, filter_only, filter_rej);	/* web server check for the first port */

		/* Another port values are comma separated */
		while ((pch = strchr(_ptr, ',')) != NULL) {
			port_length = strchr(pch, '/') - (pch + 2);
			memcpy(port_to_check, pch + 2, port_length);
			nmap_web_server_check(ip_to_check, port_to_check, filter_only, filter_rej);	/* web server check for the another ports */
			_ptr = pch + 1;
		}

	} else {
		ERR_PRINT("An error has been occured: invalid nmap version\n");
		return ERROR;
	}

	return SUCCESS;
}

int nmap_web_server_check(char *ip, char *port_str, char *filter_only, char *filter_rej) {

	int sock            = 0;
	int port            = 0;
	int num_bytes       = 0;
	char buf[4096]      = {0};

	fd_set fdset;
	struct timeval tv;

	struct sockaddr_in serv_addr;

	port = (int)strtol(port_str, NULL, 10);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		ERR_PRINT("Failed to create socket\n");
		return ERROR;
	}
	fcntl(sock, F_SETFL, O_NONBLOCK);

	/* build the server's Internet address */
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_port          = htons(port);
	serv_addr.sin_family        = AF_INET;
	serv_addr.sin_addr.s_addr   = inet_addr(ip);

	/* connect: create a connection with the server */
	connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

	FD_ZERO(&fdset);
	FD_SET(sock, &fdset);
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	if (select(sock + 1, NULL, &fdset, NULL, &tv) == 1) {
		int so_error;
		socklen_t len = sizeof(so_error);

		getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);

		if (so_error != 0) {
			DBG_PRINT("Connection was unsuccessful\n");
			close(sock);
			return ERROR;
		}
	}

	snprintf(buf, sizeof(buf), "GET / HTTP/1.0\r\n\r\n");

	/* send the message line to the server */
	num_bytes = write(sock, buf, strlen(buf));
	if (num_bytes < 0) {
		DBG_PRINT("Send data error\n");
		close(sock);
		return ERROR;
	}

	/* print the server's reply */
	if (select(sock + 1, &fdset, NULL, NULL, &tv) == 1) {
		if (FD_ISSET(sock, &fdset)) {
			/* receiving */
			buf[0] = 0;
			num_bytes = read(sock, buf, sizeof(buf));
			if (num_bytes < 0) {
				DBG_PRINT("Receive data error\n");
				close(sock);
				return ERROR;
			}

			/* PLEASE PAY YOUR ATTENTION! */
			/* The code below should be tested as well as we can */
			if (strstr(buf, "OK") ||
					strstr(buf, "Ok") ||
					strstr(buf, "ok") ||
					strstr(buf, "200")) {
				if (is_allowed(buf, filter_only, filter_rej)) {
					char log_cmd[255] = {0};
					if (strstr(buf, "WWW-Authenticate: Basic")) {
						snprintf(log_cmd, sizeof(log_cmd), "echo %s:%d >> auth_file", ip, port);
					} else {
						snprintf(log_cmd, sizeof(log_cmd), "echo %s:%d >> log_file", ip, port);
					}
					system(log_cmd);
					printf("===============> %s:%d\n", ip, port);
				} else {
					DBG_PRINT("IP %s rejected!\n", ip);
				}
			}

		}
	} else {
		DBG_PRINT("Receiving timeout, skipping..\n");
		close(sock);
		return ERROR;
	}

	close(sock);
}

int is_allowed(const char *buf, char *filter_only, char *filter_rej) {

	int res						= 0;
	int offset					= 0;
	int allowed					= FALSE;		/* is everything OK? */
	int only_allowed			= FALSE;
	int rej_allowed				= FALSE;
	char word[MAX_FILTER_LEN]	= {0};

	if (filter_only) {
		/* searching needed */
		while ((res = sscanf(filter_only + offset, "%s", word)) != -1) {
			offset += strlen(word) + 1;
			if (word && word[0] != '#' && (strlen(word) > 2)) {		/* if it is not comment */
				if (strstr(buf, word)) {							/* if we've found at least one needed word */
					only_allowed = 1;
//					printf("-> needed found!\n");
					break;
				} else {
					only_allowed = 0;
				}
			}
		}
	} else {
		only_allowed = 1;
	}

	offset = 0;
	if (filter_rej) {
		/* searching rejected */
		while ((res = sscanf(filter_rej + offset, "%s", word)) != -1) {
			offset += strlen(word) + 1;
			if (word && word[0] != '#' && (strlen(word) > 2)) {		/* if it is not comment */
				if (strstr(buf, word)) {							/* if we've found at least one needed word */
					rej_allowed = 0;
//					printf("rejected found on word >%s<, block..\n", word);
					break;
				} else {
					rej_allowed = 1;
				}
			}
		}
	}

//	printf("only %d rej %d\n", only_allowed, rej_allowed);
	if (rej_allowed && only_allowed)
		allowed = 1;

	return (filter_only || filter_rej) ? allowed : TRUE;
}

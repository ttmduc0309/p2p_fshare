#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <net/if.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <mongoc/mongoc.h>

#define MAXLINE 4096   /*max text line length*/
#define SERV_PORT 3000 /*port*/
#define CLI_PORT 3001  /*port*/
#define LISTENQ 8      /*maximum number of client connections*/
#define ERROR -1

int sockfd;
char user_id[MAXLINE];

int main(int argc, char **argv)
{
    // basic check of the arguments
    // additional checks can be inserted
    if (argc != 3)
    {
        perror("Usage: TCPClient <IP address of the server");
        exit(1);
    }

    // ------------------------------------------------------------------------
    // Get the ipaddress of this machine
    int n;
    struct ifreq ifr;

    n = socket(AF_INET, SOCK_DGRAM, 0);
    // Type of address to retrieve - IPv4 IP address
    ifr.ifr_addr.sa_family = AF_INET;
    // Copy the interface name in the ifreq structure
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
    ioctl(n, SIOCGIFADDR, &ifr);
    close(n);
    char *ip_add = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);

    int listen_port;
    // Error management
    bson_error_t error;

    // -------------------------------------------------------------------------------------------------
    // Connect client to server

    int sockfd;
    struct sockaddr_in servaddr, hostaddr;
    char input[MAXLINE], output[MAXLINE];
    socklen_t clilen;
    pid_t pid;
    int sockaddr_len = sizeof(struct sockaddr_in);
    // --------------------------------------------------------------------------------------
    // Connecting to the server

    // Create a socket for the client
    // If sockfd<0 there was an error in the creation of the socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Problem in creating the socket");
        exit(2);
    }

    // Creation of the socket
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(argv[1]);
    servaddr.sin_port = htons(atoi(argv[2])); // convert to big-endian order

    // Connection of the client to the socket
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("Problem in connecting to the server");
        exit(3);
    }
    // --------------------------------------------------------------------------------------

    // ------------------------------------------------------------------------------------------------
    // Login and Register
    while (1)
    {
        fflush(stdin);
        char username[MAXLINE], password[MAXLINE], msg[MAXLINE], server_msg[MAXLINE], port_str[MAXLINE];
        int action;
        printf("1.REGISTER\n or\n 2.LOGIN:\n");
        printf("Enter Action:\n");
        scanf("%d", &action);
        switch (action)
        {
        case 1:
        {
            strcpy(msg, "register");
            strcat(msg, " ");
            printf("Enter username: ");
            scanf("%s", username);
            strcat(msg, username);
            strcat(msg, " ");
            printf("Enter password: ");
            scanf("%s", password);
            strcat(msg, password);
            strcat(msg, " ");
            strcat(msg, ip_add);
            strcat(msg, " ");
            printf("Enter listen port no: ");
            scanf("%d", &listen_port);
            sprintf(port_str, "%d", listen_port);
            strcat(msg, port_str);
            send(sockfd, msg, sizeof(msg), 0);
            break;
        }
        case 2:
            strcpy(msg, "login");
            strcat(msg, " ");
            printf("Enter username: ");
            scanf("%s", username);
            strcat(msg, username);
            strcat(msg, " ");
            printf("Enter password: ");
            scanf("%s", password);
            strcat(msg, password);
            strcat(msg, " ");
            strcat(msg, ip_add);
            strcat(msg, " ");
            printf("Enter listen port no: ");
            scanf("%d", &listen_port);
            sprintf(port_str, "%d", listen_port);
            strcat(msg, port_str);
            send(sockfd, msg, sizeof(msg), 0);
            break;
        default:
            break;
        }
        recv(sockfd, server_msg, MAXLINE, 0);
        char *section = strtok(server_msg, " ");
        if (strcmp(section, "login_ack") == 0)
        {
            section = strtok(NULL, " ");
            if (strcmp(section, "failed") == 0)
                printf("Login Failed\n");
            else
            {
                section = strtok(NULL, " ");
                strcpy(user_id, section);
                printf("Login Success\n");
                break;
            }
        }
        else if (strcmp(section, "register_ack") == 0)
        {
            section = strtok(NULL, " ");
            if (strcmp(section, "failed") == 0)
                printf("Register Failed\n");
            else
            {
                section = strtok(NULL, " ");
                strcpy(user_id, section);
                printf("Register Success\n");
                break;
            }
        }
    }

    // --------------------------------------------------------------------------------------
    // Peer acting as a Server

    int listenfd;
    int len;
    fd_set master;
    fd_set readfd;
    struct sockaddr_in client;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Problem in creating the socket");
        exit(2);
    }

    // preparation of the socket address
    hostaddr.sin_family = AF_INET;
    hostaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    hostaddr.sin_port = htons(listen_port);
    bzero(&hostaddr.sin_zero, 8); // padding zeros

    // bind the socket
    if (bind(listenfd, (struct sockaddr *)&hostaddr, sizeof(hostaddr)) == ERROR)
    {
        perror("bind");
        exit(-1);
    }

    if ((listen(listenfd, LISTENQ)) == ERROR) // listen for max connections
    {
        perror("listen");
        exit(-1);
    }

    FD_ZERO(&master);
    FD_SET(listenfd, &master);

    if ((pid = fork()) == 0)
    {
        while (1)
        {
            char req_user[MAXLINE], req_file[MAXLINE], fetch_mes[MAXLINE], return_msg[MAXLINE] = "request_ack ", server_process[MAXLINE];
            memset(fetch_mes, 0, sizeof(fetch_mes));
            readfd = master;
            if (select(FD_SETSIZE, &readfd, NULL, NULL, NULL) == ERROR)
            {
                perror("select");
                return -1;
            }
            for (int i = 0; i < FD_SETSIZE; i++)
            {
                if (FD_ISSET(i, &readfd))
                {
                    if (i == listenfd)
                    {
                        int new_client;                                                                          // new socket descriptor for peer
                        if ((new_client = accept(listenfd, (struct sockaddr *)&client, &sockaddr_len)) == ERROR) // accept takes pointer to variable containing len of struct
                        {
                            perror("ACCEPT.Error accepting new connection");
                            exit(-1);
                        }
                        else
                        {
                            FD_SET(new_client, &master);
                            printf("\nNew peer connected from port no %d and IP %s\n", ntohs(client.sin_port), inet_ntoa(client.sin_addr));
                        }
                    }
                    else
                    {
                        bzero(input, MAXLINE);
                        if ((len = recv(i, input, MAXLINE, 0)) <= 0)
                        {
                            if (len == 0)
                            {
                                printf("Peer %d with IP address %s hung up\n", i, inet_ntoa(client.sin_addr));
                            }
                            else
                            {
                                perror("ERROR IN RECIEVE");
                            }
                            close(i);
                            FD_CLR(i, &master);
                        }
                        else
                        {
                            printf("Message: %s\n", input);
                            char *section = strtok(input, " ");
                            if (strcmp(section, "request") == 0)
                            {
                                section = strtok(NULL, " ");
                                strcpy(req_file, section);
                                section = strtok(NULL, " ");
                                strcpy(req_user, section);
                                printf("Request: %s\n", req_file);
                                FILE *fr;
                                fr = fopen(req_file, "r");
                                if (!fr)
                                {
                                    strcat(return_msg, " failed");
                                    send(i, return_msg, MAXLINE, 0);
                                    fprintf(stderr, "ERROR : Opening requested file.REQUESTED FILE NOT FOUND \n");
                                    close(i);           // closing this connection
                                    FD_CLR(i, &master); // remove from master set
                                    strcpy(fetch_mes, "fetch ");
                                    strcat(fetch_mes, " failed ");
                                    strcat(fetch_mes, user_id);
                                    strcat(fetch_mes, " ");
                                    strcat(fetch_mes, req_file);
                                    send(sockfd, fetch_mes, MAXLINE, 0);
                                    recv(sockfd, server_process, MAXLINE, 0);
                                    char *section = strtok(server_process, " ");
                                    section = strtok(NULL, " ");
                                    if (strcmp(section, "success") == 0)
                                        printf("Server Delete process success\n");
                                    else
                                        printf("Server Delete process failed\n");
                                }
                                else
                                {
                                    strcat(return_msg, " success");
                                    send(i, return_msg, MAXLINE, 0);
                                    char buf[MAXLINE];
                                    size_t bytesRead;

                                    while ((bytesRead = fread(buf, 1, sizeof(buf), fr)) > 0)
                                    {
                                        send(i, buf, bytesRead, 0);
                                    }

                                    fclose(fr);

                                    close(i);
                                    FD_CLR(i, &master);
                                    strcpy(fetch_mes, "fetch ");
                                    strcat(fetch_mes, req_user);
                                    strcat(fetch_mes, " ");
                                    strcat(fetch_mes, req_file);
                                    strcat(fetch_mes, " ");
                                    strcat(fetch_mes, user_id);
                                    send(sockfd, fetch_mes, MAXLINE, 0);
                                    recv(sockfd, server_process, MAXLINE, 0);
                                    char *section = strtok(server_process, " ");
                                    section = strtok(NULL, " ");
                                    if (strcmp(section, "success") == 0)
                                        printf("Server Transfer process success\n");
                                    else
                                        printf("Server Transfer process failed\n");
                                }
                            }
                        }
                    }
                }
            }
        }
        exit(0);
    }

    // ------------------------------------------------------------------------------------------------

    // ------------------------------------------------------------------------------------------------
    // Displaying app menu

    int peer_sock;
    struct sockaddr_in cliaddr;

    while (1)
    {
        char message[MAXLINE], server_message[MAXLINE];
        int cmd;
        bzero(input, MAXLINE);
        printf("1.Exit\n");
        printf("2.Add new file to server\n");
        printf("3.Delete file from server\n");
        printf("4.Query files from the server\n");
        printf("5.Fetch file\n");
        printf("Enter command: ");
        scanf("%d", &cmd);
        switch (cmd)
        {
        case 1:
        {
            kill(pid, SIGKILL);
            close(sockfd);
            break;
        }
        case 2:
        {
            strcpy(message, "add");
            char file_add[MAXLINE];
            char file_addr[MAXLINE];
            char file_port[MAXLINE];

            fflush(stdin);
            printf("Enter file to be added: ");
            scanf("%s", file_add);
            strcat(message, " ");
            strcat(message, file_add);
            strcat(message, " ");
            strcat(message, user_id);
            send(sockfd, message, strlen(message), 0);
            recv(sockfd, server_message, MAXLINE, 0);
            char *section = strtok(server_message, " ");
            if (strcmp(server_message, "add_ack") == 0)
            {
                section = strtok(NULL, " ");
                if (strcmp(section, "failed") == 0)
                    printf("File Addition Failed\n");
                else if (strcmp(section, "success") == 0)
                    printf("File Addition Successful\n");
            }
            else
                printf("Inapropriate response\n");
            break;
        }
        case 3:
        {
            strcpy(message, "delete");
            char file_find[MAXLINE];
            printf("Enter file to delete: ");
            fflush(stdin);
            scanf("%s", file_find);
            strcat(message, " ");
            strcat(message, file_find);
            strcat(message, " ");
            strcat(message, user_id);
            send(sockfd, message, strlen(message), 0);
            recv(sockfd, server_message, MAXLINE, 0);
            char *section = strtok(server_message, " ");
            if (strcmp(server_message, "delete_ack") == 0)
            {
                section = strtok(NULL, " ");
                if (strcmp(section, "failed") == 0)
                    printf("File Deletion Failed\n");
                else if (strcmp(section, "success") == 0)
                    printf("File Deletion Successful\n");
            }
            else
                printf("Inapropriate response\n");
            break;
            break;
        }
        case 4:
        {
            strcpy(message, "query");
            char query_file[MAXLINE], output[MAXLINE], qid[MAXLINE];
            bson_t log;
            bson_iter_t iter;
            printf("Enter filename to find: ");
            scanf("%s", query_file);
            strcat(message, " ");
            strcat(message, query_file);
            send(sockfd, message, strlen(message), 0);
            int l;
            recv(sockfd, server_message, MAXLINE, 0);
            if (strcmp(server_message, "query_hit") == 0)
            {
                printf("------------File Catalog------------\n");
                while ((l = recv(sockfd, output, MAXLINE, 0)) > 0)
                {
                    if (strcmp(output, "end") == 0)
                        break;
                    printf("\n-------------------------------------\n");
                    bson_init_from_json(&log, output, -1, &error);
                    if (bson_iter_init(&iter, &log))
                    {
                        // Iterate over the fields
                        while (bson_iter_next(&iter))
                        {
                            const char *key = bson_iter_key(&iter);
                            if (strcmp(key, "password") != 0 && strcmp(key, "_id") != 0)
                            {
                                printf("%s: ", key);
                                bson_type_t type = bson_iter_type(&iter);
                                switch (type)
                                {
                                case BSON_TYPE_UTF8:
                                    printf(" %s\n", bson_iter_utf8(&iter, NULL));
                                    break;
                                case BSON_TYPE_INT32:
                                    printf(" %d\n", bson_iter_int32(&iter));
                                    break;
                                case BSON_TYPE_OID:
                                    bson_oid_t newid;
                                    bson_oid_copy(bson_iter_oid(&iter), &newid);
                                    bson_oid_to_string(&newid, qid);
                                    printf(" %s\n", qid);
                                    break;
                                // Add cases for other BSON types as needed
                                default:
                                    break;
                                }
                            }
                        }
                    }
                    memset(output, 0, sizeof(output));
                }
            }
            else if (strcmp(server_message, "query_miss") == 0)
                printf("File not found\n");
            break;
        }
        case 5:
        {
            char file_fet[MAXLINE], peer_ip[MAXLINE], peer_port[MAXLINE], fetch_msg[MAXLINE] = "request ", peer_msg[MAXLINE];
            fflush(stdin);
            printf("Enter file to be fetched: ");
            scanf(" %[^\t\n]s", file_fet);
            printf("Enter peer IP address: ");
            scanf(" %[^\t\n]s", peer_ip);
            printf("Enter peer listening port number: ");
            scanf(" %[^\t\n]s", peer_port);

            if ((peer_sock = socket(AF_INET, SOCK_STREAM, 0)) == ERROR)
            {
                perror("socket");
                // error checking the socket
                kill(pid, SIGKILL); // on exit, the created lsitening process to be killed
                exit(-1);
            }

            cliaddr.sin_family = AF_INET;                                                 // family
            cliaddr.sin_port = htons(atoi(peer_port));                                    // Port No and htons to convert from host to network byte order. atoi to convert asci to integer
            cliaddr.sin_addr.s_addr = inet_addr(peer_ip);                                 // IP addr in ACSI form to network byte order converted using inet
            bzero(&cliaddr.sin_zero, 8);                                                  // padding zeros
            if ((connect(peer_sock, (struct sockaddr *)&cliaddr, sockaddr_len)) == ERROR) // pointer casted to sockaddr*
            {
                perror("connect");
                kill(pid, SIGKILL); // on exit, the created lsitening process to be killed
                exit(-1);
            }

            strcat(fetch_msg, file_fet);
            strcat(fetch_msg, " ");
            strcat(fetch_msg, user_id);

            send(peer_sock, fetch_msg, strlen(fetch_msg), 0);
            printf("Recieving file from peer. Please wait \n");
            if (recv(peer_sock, peer_msg, MAXLINE, 0) > 0)
            {
                char *section = strtok(peer_msg, " ");
                section = strtok(NULL, " ");
                if (strcmp(section, "success") == 0)
                {
                    FILE *fetch_file = fopen(file_fet, "w");
                    if (fetch_file == NULL) // error creating file
                    {
                        printf("File %s cannot be created.\n", file_fet);
                    }

                    else
                    {
                        int file_fetch_size = 0;
                        int len_recd = 0;

                        while ((len_recd = recv(peer_sock, input, MAXLINE, 0)) > 0)
                        {
                            fwrite(input, 1, len_recd, fetch_file);
                        }

                        if (len_recd < 0)
                        {
                            perror("Read error");
                            exit(1);
                        }

                        fclose(fetch_file); // close opened file
                        printf("FETCH COMPLETE\n");
                        close(peer_sock); // close socket
                    }
                }
                else
                    printf("Fetch Failed\n");
            }
            break;
        }
        default:
            printf("Wrong command");
            break;
        }
        if (cmd == 1)
            break;
    }
    close(listenfd);

    exit(0);
}
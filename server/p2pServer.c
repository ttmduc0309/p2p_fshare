#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>
#include <mongoc/mongoc.h>

#define MAXLINE 4096   /*max text line length*/
#define SERV_PORT 3000 /*port*/
#define LISTENQ 8      /*maximum number of client connections*/
#define MY_MONGODB_URI "mongodb+srv://dantdm2002:gNQBMlXfbmNPFzoa@cluster0.sqx5s5z.mongodb.net/?retryWrites=true&w=majority"

static char hexbyte(char hex)
{
    switch (hex)
    {
    case '0':
        return 0x0;
    case '1':
        return 0x1;
    case '2':
        return 0x2;
    case '3':
        return 0x3;
    case '4':
        return 0x4;
    case '5':
        return 0x5;
    case '6':
        return 0x6;
    case '7':
        return 0x7;
    case '8':
        return 0x8;
    case '9':
        return 0x9;
    case 'a':
    case 'A':
        return 0xa;
    case 'b':
    case 'B':
        return 0xb;
    case 'c':
    case 'C':
        return 0xc;
    case 'd':
    case 'D':
        return 0xd;
    case 'e':
    case 'E':
        return 0xe;
    case 'f':
    case 'F':
        return 0xf;
    default:
        return 0x0; /* something smarter? */
    }
}

void sig_chld(int signo)
{
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
        printf("child %d terminated\n", pid);
    return;
}
void bson_oid_from_string(bson_oid_t *oid, const char *str)
{
    int i;
    for (i = 0; i < 12; i++)
    {
        oid->bytes[i] = (hexbyte(str[2 * i]) << 4) | hexbyte(str[2 * i + 1]);
    }
}

int main(int argc, char **argv)
{
    // ------------------------------------------------------------------------
    // Connect mongodb

    // your MongoDB URI connection string
    const char *uri_string = MY_MONGODB_URI;
    // MongoDB URI created from above string
    mongoc_uri_t *uri;
    // MongoDB Client, used to connect to the DB
    mongoc_client_t *client;

    // Error management
    bson_error_t error;

    mongoc_database_t *database;
    mongoc_collection_t *collection;
    char **collection_names;
    mongoc_cursor_t *cursor;
    unsigned i;

    // Object id and BSON doc
    bson_oid_t oid;
    bson_t *doc;
    bson_t *query;
    bson_t *update;

    char *str;

    /*
     * Required to initialize libmongoc's internals
     */
    mongoc_init();

    /*
     * Safely create a MongoDB URI object from the given string
     */
    uri = mongoc_uri_new_with_error(uri_string, &error);
    if (!uri)
    {
        fprintf(stderr,
                "failed to parse URI: %s\n"
                "error message:       %s\n",
                uri_string, error.message);
        return EXIT_FAILURE;
    }

    /*
     * Create a new client instance, here we use the uri we just built
     */
    client = mongoc_client_new_from_uri(uri);
    if (!client)
    {
        return EXIT_FAILURE;
    }

    /*
     * Get a handle on the database "db_name" and collection "coll_name"
     */
    database = mongoc_client_get_database(client, "network_info");

    /*
     * Register the application name so we can track it in the profile logs
     * on the server. This can also be done from the URI (see other examples).
     */
    mongoc_client_set_appname(client, "connect-example");

    // ------------------------------------------------------------------------
    // Create server

    int listenfd, connfd, n;
    pid_t childpid;
    socklen_t clilen;
    char buf[MAXLINE];
    struct sockaddr_in cliaddr, servaddr;

    // Create a socket for the soclet
    // If sockfd<0 there was an error in the creation of the socket
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Problem in creating the socket");
        exit(2);
    }

    // preparation of the socket address
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);

    // bind the socket
    bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    // listen to the socket by creating a connection queue, then wait for clients
    listen(listenfd, LISTENQ);

    printf("%s\n", "Server running...waiting for connections.");

    for (;;)
    {

        clilen = sizeof(cliaddr);
        // accept a connection
        connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);

        printf("%s\n", "Received request...");

        if ((childpid = fork()) == 0)
        { // if it’s 0, it’s child process
            char user_id[MAXLINE];
            printf("%s\n", "Child created for dealing with client requests");

            // close listening socket
            close(listenfd);

            while ((n = recv(connfd, buf, sizeof(buf), 0)) > 0)
            {
                printf("Client %s: %s\n", user_id, buf);
                char *trun = strtok(buf, " ");

                if (strcmp(trun, "add") == 0)
                {
                    char add_msg[MAXLINE] = "add_ack";
                    bson_oid_t new_oid;
                    const bson_t *query_const;
                    collection = mongoc_client_get_collection(client, "network_info", "file_info");
                    doc = bson_new();
                    bson_oid_init(&oid, NULL);
                    BSON_APPEND_OID(doc, "_id", &oid);
                    trun = strtok(NULL, " ");
                    BSON_APPEND_UTF8(doc, "name", trun);
                    query = bson_new();
                    BSON_APPEND_UTF8(query, "name", trun);
                    trun = strtok(NULL, " ");
                    bson_oid_from_string(&new_oid, trun);
                    BSON_APPEND_OID(query, "user_id", &new_oid);
                    BSON_APPEND_OID(doc, "user_id", &new_oid);

                    cursor = mongoc_collection_find_with_opts(collection, query, NULL, NULL);
                    if (!mongoc_cursor_next(cursor, &query_const))
                    {
                        if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error))
                        {
                            strcat(add_msg, " failed");
                            send(connfd, add_msg, MAXLINE, 0);
                            fprintf(stderr, "%s\n", error.message);
                        }
                        else
                        {
                            strcat(add_msg, " success");
                            send(connfd, add_msg, MAXLINE, 0);
                            printf("Document inserted!\n");
                            /*
                             * Print the document as a JSON string.
                             */
                            str = bson_as_canonical_extended_json(doc, NULL);
                            printf("%s\n", str);
                            bson_free(str);
                        }
                    }
                    else
                    {
                        strcat(add_msg, " failed");
                        send(connfd, add_msg, MAXLINE, 0);
                        printf("This file already exist\n");
                    }
                }
                else if (strcmp(trun, "delete") == 0)
                {
                    bson_oid_t new_oid;
                    char delete_msg[MAXLINE] = "delete_ack";
                    collection = mongoc_client_get_collection(client, "network_info", "file_info");
                    doc = bson_new();
                    trun = strtok(NULL, " ");
                    BSON_APPEND_UTF8(doc, "name", trun);
                    trun = strtok(NULL, " ");
                    bson_oid_from_string(&new_oid, trun);
                    BSON_APPEND_OID(doc, "user_id", &new_oid);
                    if (!mongoc_collection_delete_many(collection, doc, NULL, NULL, &error))
                    {
                        strcat(delete_msg, " failed");
                        send(connfd, delete_msg, MAXLINE, 0);
                        printf("Deletion failed\n");
                        fprintf(stderr, "Delete failed: %s\n", error.message);
                    }
                    else
                    {
                        strcat(delete_msg, " success");
                        send(connfd, delete_msg, MAXLINE, 0);
                        puts("Document deleted!\n");
                    }
                }
                else if (strcmp(trun, "query") == 0)
                {
                    bson_iter_t iterinner, fid;
                    char end_message[MAXLINE] = "end", queryhit_msg[MAXLINE] = "query_hit", querymiss_msg[MAXLINE] = "query_miss";
                    const bson_t *query_const;
                    const bson_oid_t *oid_ptr;
                    collection = mongoc_client_get_collection(client, "network_info", "file_info");
                    query = bson_new();
                    trun = strtok(NULL, " ");
                    BSON_APPEND_UTF8(query, "name", trun);
                    cursor = mongoc_collection_find_with_opts(collection, query, NULL, NULL);
                    if (cursor && mongoc_cursor_next(cursor, &query_const))
                    {
                        send(connfd, queryhit_msg, sizeof(queryhit_msg), 0);
                        do
                        {
                            bson_t *result;
                            result = bson_new();
                            bson_concat(result, query_const);
                            if (bson_iter_init(&iterinner, query_const) && bson_iter_find_descendant(&iterinner, "user_id", &fid))
                            {
                                oid_ptr = bson_iter_oid(&fid);
                                bson_t *user_query;
                                user_query = bson_new();
                                const bson_t *user_const;
                                BSON_APPEND_OID(user_query, "_id", oid_ptr);
                                mongoc_collection_t *collection_small;
                                collection_small = mongoc_client_get_collection(client, "network_info", "user_info");
                                mongoc_cursor_t *cursor_small = mongoc_collection_find_with_opts(collection_small, user_query, NULL, NULL);

                                if (mongoc_cursor_next(cursor_small, &user_const))
                                    bson_concat(result, user_const);
                            }
                            str = bson_as_canonical_extended_json(result, NULL);
                            send(connfd, str, MAXLINE, 0);
                            bson_free(str);
                        } while (mongoc_cursor_next(cursor, &query_const));
                        send(connfd, end_message, sizeof(end_message), 0);
                    }
                    else
                    {
                        send(connfd, querymiss_msg, sizeof(querymiss_msg), 0);
                        printf("Not Found\n");
                    }
                }
                else if (strcmp(trun, "register") == 0)
                {
                    char register_msg[MAXLINE] = "register_ack", uid_string[MAXLINE];
                    query = bson_new();
                    collection = mongoc_client_get_collection(client, "network_info", "user_info");
                    trun = strtok(NULL, " ");
                    BSON_APPEND_UTF8(query, "username", trun);
                    const bson_t *query_const = query;
                    cursor = mongoc_collection_find_with_opts(collection, query, NULL, NULL);
                    if (mongoc_cursor_next(cursor, &query_const))
                    {
                        strcat(register_msg, " failed");
                        send(connfd, register_msg, MAXLINE, 0);
                        printf("This account already exist\n");
                    }
                    else
                    {
                        doc = bson_new();
                        bson_oid_init(&oid, NULL);
                        BSON_APPEND_OID(doc, "_id", &oid);
                        BSON_APPEND_UTF8(doc, "username", trun);
                        trun = strtok(NULL, " ");
                        BSON_APPEND_UTF8(doc, "password", trun);
                        // trun = strtok(NULL, " ");
                        // char *client_ip ;
                        char client_ip[INET_ADDRSTRLEN];
                        // = inet_ntoa(cliaddr.sin_addr);
                        inet_ntop(AF_INET, &cliaddr.sin_addr, client_ip, INET_ADDRSTRLEN);
                        printf("Client IP address: %s\n", client_ip);
                        BSON_APPEND_UTF8(doc, "address", client_ip);
                        trun = strtok(NULL, " ");
                        int listen_port = atoi(trun);
                        BSON_APPEND_INT32(doc, "port_number", listen_port);
                        if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error))
                        {
                            strcat(register_msg, " failed");
                            send(connfd, register_msg, MAXLINE, 0);
                            fprintf(stderr, "%s\n", error.message);
                        }
                        else
                        {
                            strcat(register_msg, " success ");
                            bson_oid_to_string(&oid, uid_string);
                            strcpy(user_id, uid_string);
                            strcat(register_msg, uid_string);
                            send(connfd, register_msg, MAXLINE, 0);
                            printf("Account inserted!\n");
                            str = bson_as_canonical_extended_json(doc, NULL);
                            printf("%s\n", str);
                            bson_free(str);
                        }
                    }
                }
                else if (strcmp(trun, "login") == 0)
                {
                    char login_msg[MAXLINE] = "login_ack", uid_string[MAXLINE];
                    bson_t child;
                    query = bson_new();
                    collection = mongoc_client_get_collection(client, "network_info", "user_info");
                    const bson_t *query_const = query;
                    trun = strtok(NULL, " ");
                    BSON_APPEND_UTF8(query, "username", trun);
                    cursor = mongoc_collection_find_with_opts(collection, query, NULL, NULL);

                    if (mongoc_cursor_next(cursor, &query_const))
                    {
                        bson_iter_t iter;
                        bson_iter_t fpass, fid;
                        uint32_t length;
                        const bson_oid_t *oid_ptr;
                        bson_oid_t uid;
                        if (bson_iter_init(&iter, query_const) && bson_iter_find_descendant(&iter, "password", &fpass))
                        {
                            trun = strtok(NULL, " ");
                            if (strcmp(trun, bson_iter_utf8(&fpass, &length)) == 0)
                            {
                                if (bson_iter_init(&iter, query_const) && bson_iter_find_descendant(&iter, "_id", &fid))
                                {
                                    oid_ptr = bson_iter_oid(&fid);
                                    memcpy(&uid, oid_ptr, sizeof(bson_oid_t));
                                    update = bson_new();
                                    BSON_APPEND_DOCUMENT_BEGIN(update, "$set", &child);
                                    // trun = strtok(NULL, " ");
                                    // char *client_ip = inet_ntoa(cliaddr.sin_addr);
                                    // char *client_ip;
                                    // = inet_ntoa(cliaddr.sin_addr);
                                    char client_ip[INET_ADDRSTRLEN];
                                    inet_ntop(AF_INET, &cliaddr.sin_addr, client_ip, INET_ADDRSTRLEN);
                                    printf("Client IP address: %s\n", client_ip);
                                    BSON_APPEND_UTF8(&child, "address", client_ip);
                                    trun = strtok(NULL, " ");
                                    int listen_port = atoi(trun);
                                    BSON_APPEND_INT32(&child, "port_number", listen_port);
                                    bson_append_document_end(update, &child);
                                    if (!mongoc_collection_update_one(collection, query, update, NULL, NULL, &error))
                                    {
                                        strcat(login_msg, " failed");
                                        send(connfd, login_msg, MAXLINE, 0);
                                        fprintf(stderr, "%s\n", error.message);
                                    }
                                    else
                                    {
                                        strcat(login_msg, " success ");
                                        bson_oid_to_string(&uid, uid_string);
                                        strcat(login_msg, uid_string);
                                        strcpy(user_id, uid_string);
                                        send(connfd, login_msg, MAXLINE, 0);
                                        printf("Account edited!\n");
                                        str = bson_as_canonical_extended_json(update, NULL);
                                        printf("%s\n", str);
                                        bson_free(str);
                                    }
                                }
                            }
                            else
                            {
                                strcat(login_msg, " failed");
                                send(connfd, login_msg, MAXLINE, 0);
                                printf("Wrong password\n");
                            }
                        }
                    }
                    else
                    {
                        strcat(login_msg, " failed");
                        send(connfd, login_msg, MAXLINE, 0);
                        printf("Account not found\n");
                    }
                }
                else if (strcmp(trun, "fetch") == 0)
                {
                    collection = mongoc_client_get_collection(client, "network_info", "file_info");
                    bson_oid_t req_id, acc_id;
                    bson_t child;
                    char req_file[MAXLINE], req_user[MAXLINE], acc_user[MAXLINE], fetch_ack_msg[MAXLINE] = "fetch_ack ";
                    trun = strtok(NULL, " ");
                    if (strcmp(trun, "failed") != 0)
                    {
                        strcpy(req_user, trun);
                        trun = strtok(NULL, " ");
                        strcpy(req_file, trun);
                        trun = strtok(NULL, " ");
                        strcpy(acc_user, trun);
                        doc = bson_new();
                        bson_oid_from_string(&req_id, req_user);
                        BSON_APPEND_DOCUMENT_BEGIN(doc, "$set", &child);
                        BSON_APPEND_OID(&child, "user_id", &req_id);
                        bson_append_document_end(doc, &child);
                        query = bson_new();
                        bson_oid_from_string(&acc_id, acc_user);
                        BSON_APPEND_UTF8(query, "name", req_file);
                        BSON_APPEND_OID(query, "user_id", &acc_id);
                        if (!mongoc_collection_update_one(collection, query, doc, NULL, NULL, &error))
                        {
                            strcat(fetch_ack_msg, "failed");
                            send(connfd, fetch_ack_msg, MAXLINE, 0);
                            fprintf(stderr, "%s\n", error.message);
                        }
                        else
                        {
                            strcat(fetch_ack_msg, "success");
                            send(connfd, fetch_ack_msg, MAXLINE, 0);
                            printf("Account edited!\n");
                            str = bson_as_canonical_extended_json(query, NULL);
                            printf("%s\n", str);
                            bson_free(str);
                            str = bson_as_canonical_extended_json(doc, NULL);
                            printf("%s\n", str);
                            bson_free(str);
                        }
                    }
                    else
                    {
                        bson_oid_t new_oid;
                        trun = strtok(NULL, " ");
                        strcpy(acc_user, trun);
                        trun = strtok(NULL, " ");
                        strcpy(req_file, trun);
                        doc = bson_new();
                        bson_oid_from_string(&new_oid, acc_user);
                        BSON_APPEND_OID(doc, "user_id", &new_oid);
                        BSON_APPEND_UTF8(doc, "name", req_file);
                        if (!mongoc_collection_delete_many(collection, doc, NULL, NULL, &error))
                        {
                            strcat(fetch_ack_msg, "failed");
                            send(connfd, fetch_ack_msg, MAXLINE, 0);
                            printf("Transfer failed\n");
                            fprintf(stderr, "Delete failed: %s\n", error.message);
                        }
                        else
                        {
                            strcat(fetch_ack_msg, "success");
                            send(connfd, fetch_ack_msg, MAXLINE, 0);
                            puts("Transfer deleted!\n");
                        }
                    }
                }
                memset(buf, 0, sizeof(buf));
                fflush(stdout);
            }

            if (n == 0)
            {
                bson_oid_t new_oid;
                collection = mongoc_client_get_collection(client, "network_info", "file_info");
                doc = bson_new();
                bson_oid_from_string(&new_oid, user_id);
                BSON_APPEND_OID(doc, "user_id", &new_oid);
                if (!mongoc_collection_delete_many(collection, doc, NULL, NULL, &error))
                {
                    printf("Exit Deletion failed\n");
                    fprintf(stderr, "Delete failed: %s\n", error.message);
                }
                else
                    puts("Exit Files deleted!\n");
            }

            if (n < 0)
                printf("%s\n", "Read error");
            exit(0);
        }
        // close socket of the server
        signal(SIGCHLD, sig_chld);
        close(connfd);
    }
    mongoc_uri_destroy(uri);
    mongoc_client_destroy(client);
    mongoc_cleanup();
}
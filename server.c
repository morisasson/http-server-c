#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include "threadpool.h"

#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define BUFFER_SIZE 4096

// Sending the entire content to the client
ssize_t send_all(int fd, const void *buf, size_t len) {
    const char *ptr = (const char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t written = send(fd, ptr, remaining, 0);
        if (written < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);
                continue;
            }
            return -1;
        }
        ptr += written;
        remaining -= written;
    }
    return len;
}

// MIME file types
char* get_mime_type(const char *name) {
    char *ext = strrchr(name, '.');
    if (!ext) return "text/plain";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    return "application/octet-stream";
}

// Generating the current date
void get_current_time(char* buf, size_t size) {
    time_t now = time(NULL);
    strftime(buf, size, RFC1123FMT, gmtime(&now));
}

// Sending errors
void send_error(int client_fd, int status, const char *status_text, const char *message) {
    char content[BUFFER_SIZE];
    char header[BUFFER_SIZE];
    char timebuf[128];
    int len;

    // Get current time
    get_current_time(timebuf, sizeof(timebuf));

    // For 302 redirect, handle differently
    if (status == 302) {
        // Create minimal content
        len = snprintf(content, sizeof(content),
            "<HTML><HEAD><TITLE>302 Found</TITLE></HEAD>\n"
            "<BODY><H4>302 Found</H4>\n"
            "Redirecting to %s\n</BODY></HTML>\n",
            message);  // message contains the Location path

        // Create header with Location
        snprintf(header, sizeof(header),
            "HTTP/1.0 302 Found\r\n"
            "Server: webserver/1.0\r\n"
            "Date: %s\r\n"
            "Location: %s\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            timebuf,
            message,
            len);
    } else {
        // Handle other error responses as before
        len = snprintf(content, sizeof(content),
            "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\n"
            "<BODY><H4>%d %s</H4>\n%s\n</BODY></HTML>\n",
            status, status_text, status, status_text, message);

        snprintf(header, sizeof(header),
            "HTTP/1.0 %d %s\r\n"
            "Server: webserver/1.0\r\n"
            "Date: %s\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            status, status_text, timebuf, len);
    }

    // Send header and content
    if (send_all(client_fd, header, strlen(header)) < 0) {
        return;
    }
    if (send_all(client_fd, content, len) < 0) {
        return;
    }
}
void send_file(int client_fd, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        send_error(client_fd, 404, "Not Found", "File not found.");
        return;
    }

    int file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        send_error(client_fd, 403, "Forbidden", "Access denied.");
        return;
    }

    // Send headers
    char header[BUFFER_SIZE];
    char timebuf[128];
    get_current_time(timebuf, sizeof(timebuf));

    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.0 200 OK\r\n"
                              "Server: webserver/1.0\r\n"
                              "Date: %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %ld\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              timebuf, get_mime_type(path), st.st_size);

    if (send_all(client_fd, header, header_len) < 0) {
        close(file_fd);
        return;
    }

    // Use larger buffer size for better performance
    char buffer[65536];  // 64KB buffer
    ssize_t bytes_read;

    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        if (send_all(client_fd, buffer, bytes_read) < 0) {
            break;
        }
    }

    close(file_fd);
}

void send_directory_listing(int client_fd, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        send_error(client_fd, 500, "Internal Server Error", "Failed to open directory.");
        return;
    }

    char content[BUFFER_SIZE * 10];
    int len = snprintf(content, sizeof(content),
        "<HTML>\n<HEAD><TITLE>Index of %s</TITLE></HEAD>\n<BODY>\n<H4>Index of %s</H4>\n"
        "<table CELLSPACING=8>\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\n",
        path, path);

    struct dirent *entry;
    struct stat st;
    char fullpath[BUFFER_SIZE];
    char timebuf[128];

// Link to the previous directory
    len += snprintf(content + len, sizeof(content) - len,
        "<tr><td><A HREF=\"..\">Parent Directory</A></td><td></td><td></td></tr>\n");

// Iterating over all directory items
    while ((entry = readdir(dir)) != NULL) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        if (stat(fullpath, &st) == 0) {
            strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&st.st_mtime));

            if (S_ISDIR(st.st_mode)) {
                len += snprintf(content + len, sizeof(content) - len,
                    "<tr><td><A HREF=\"%s/\">%s/</A></td><td>%s</td><td>-</td></tr>\n",
                    entry->d_name, entry->d_name, timebuf);
            } else {
                len += snprintf(content + len, sizeof(content) - len,
                    "<tr><td><A HREF=\"%s\">%s</A></td><td>%s</td><td>%ld</td></tr>\n",
                    entry->d_name, entry->d_name, timebuf, st.st_size);
            }
        }
    }

    len += snprintf(content + len, sizeof(content) - len, "</table>\n<HR>\n<ADDRESS>webserver/1.0</ADDRESS>\n</BODY></HTML>\n");

// Sending the header and content
    char header[BUFFER_SIZE];
    char timebuf_header[128];
    get_current_time(timebuf_header, sizeof(timebuf_header));

    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Server: webserver/1.0\r\n"
        "Date: %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        timebuf_header, len);

    send_all(client_fd, header, header_len);
    send_all(client_fd, content, len);

    closedir(dir);
}
// Handling a directory
void handle_directory(int client_fd, const char *path) {
    char index_path[BUFFER_SIZE];
    snprintf(index_path, sizeof(index_path), "%s/index.html", path);

    struct stat st;
    if (stat(index_path, &st) == 0 && S_ISREG(st.st_mode)) {
        // If index.html exists - send it
        send_file(client_fd, index_path);
    } else {
        // Otherwise, display the directory contents
        send_directory_listing(client_fd, path);
    }
}

void handle_request(int client_fd) {
    // Initialize buffer
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Read the request line
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes_read] = '\0';

    // Parse the request
    char method[16], path[256], protocol[16];
    if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) != 3) {
        send_error(client_fd, 400, "Bad Request", "Invalid request format");
        close(client_fd);
        return;
    }

    // Check method
    if (strcmp(method, "GET") != 0) {
        send_error(client_fd, 501, "Not Supported", "Method is not supported.");
        close(client_fd);
        return;
    }

    // Handle path
    char real_path[BUFFER_SIZE];
    snprintf(real_path, sizeof(real_path), ".%s", path);

    struct stat st;
    if (stat(real_path, &st) != 0) {
        send_error(client_fd, 404, "Not Found", "File not found.");
        close(client_fd);
        return;
    }

    // Handle directory
    if (S_ISDIR(st.st_mode)) {
        if (path[strlen(path) - 1] != '/') {
            char redirect_path[BUFFER_SIZE];
            snprintf(redirect_path, sizeof(redirect_path), "%s/", path);
            send_error(client_fd, 302, "Found", redirect_path);
        } else {
            handle_directory(client_fd, real_path);
        }
    } else {
        // Handle file
        send_file(client_fd, real_path);
    }

    close(client_fd);
}
int handle_request_wrapper(void* arg) {
    int fd = *((int*)arg);
    handle_request(fd);
    free(arg);
    return 0;  // Or return the appropriate error code if needed
}
int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr,"Usage: server <port> <pool-size> <queue-size>\n");
        exit(EXIT_FAILURE);
    }

// Reading parameters from the command line
    int port = atoi(argv[1]);
    int pool_size = atoi(argv[2]);
    int max_queue_size = atoi(argv[3]);
    int max_requests = atoi(argv[4]);

    if (port <= 0 || port > 65535 || pool_size <= 0 || max_requests <= 0) {
        fprintf(stderr, "Usage: server <port> <pool-size> <queue-size> <max-requests>\n");
        exit(EXIT_FAILURE); }

// Creating a socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

// Setting the option for address reuse
    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

// Configuring the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

// Binding the socket to the address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

// Starting to listen for requests
    if (listen(server_fd, max_queue_size) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }


// Creating a Thread Pool
    threadpool *pool = create_threadpool(pool_size, max_queue_size);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

// Handling client requests
    int requests_handled = 0;
    while (requests_handled < max_requests) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            perror("malloc");
            continue;
        }

        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            perror("accept");
            free(client_fd);
            continue;
        }

        dispatch(pool, handle_request_wrapper, client_fd);
        requests_handled++;
    }
// Finalizing work and destroying resources
    destroy_threadpool(pool);
    close(server_fd);

    return 0;
}

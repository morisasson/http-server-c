HTTP Server in C

Overview
This project implements a multi-threaded HTTP server in C that can handle multiple client connections using a thread pool. The server processes HTTP GET requests, serves files, and generates directory listings. It also supports error handling for various HTTP status codes.

Features:
✔ Handles multiple concurrent client connections using a thread pool.
✔ Supports GET requests for files and directories.
✔ Handles HTTP status codes: 200 OK, 400 Bad Request, 403 Forbidden, 404 Not Found, 500 Internal Server Error, 501 Not Supported.
✔ Redirects directories without a trailing slash (302 Found).
✔ Generates directory listings dynamically if no index.html is present.
✔ Supports MIME type detection for served files.
✔ Implements a request queue for managing client connections efficiently.

Files Overview:
- server.c         -> Main server implementation
- threadpool.h     -> Header file for the thread pool
- 400.txt          -> HTTP 400 error response
- 403.txt          -> HTTP 403 error response
- 404.txt          -> HTTP 404 error response
- 500.txt          -> HTTP 500 error response
- 501.txt          -> HTTP 501 error response
- 302.txt          -> HTTP 302 redirect response
- dir_content.txt  -> Directory listing format
- file.txt         -> Template for file responses
- README.txt       -> Project documentation

How to Compile and Run:

Prerequisites:
- Linux/macOS terminal (or MinGW for Windows users)
- GCC compiler installed
- POSIX thread support (-lpthread)

Compilation:
gcc -Wall -o server server.c -lpthread

Running the Server:
./server <port> <pool-size> <max-queue-size> <max-requests>

Command-Line Arguments:
- <port>  -> Port number the server will listen on
- <pool-size>  -> Number of worker threads in the pool
- <max-queue-size>  -> Maximum size of the request queue
- <max-requests>  -> Maximum number of requests the server will handle before shutting down

Example Usage:
./server 8080 5 10 100

This starts the server on port 8080, with 5 threads, a queue size of 10, and will process 100 requests before exiting.

Handling HTTP Requests:
1️⃣ GET Requests – Retrieves files or directories from the server.
2️⃣ Directory Handling:
   - If a directory is requested without a trailing /, the server redirects (302 Found).
   - If a directory contains index.html, it is served.
   - Otherwise, a directory listing is generated dynamically.
3️⃣ File Handling:
   - Supports various MIME types (.html, .jpg, .png, .css, etc.).
   - Ensures permissions before serving a file (403 Forbidden if restricted).
4️⃣ Error Handling:
   - Malformed requests → 400 Bad Request
   - Nonexistent resources → 404 Not Found
   - Unsupported methods → 501 Not Supported
   - Internal failures → 500 Internal Server Error

Error Handling:
The server handles errors gracefully and responds with appropriate HTTP status codes.

✅ Example: File Not Found (404)
curl -v http://localhost:8080/nonexistent.html

📌 Response:
HTTP/1.0 404 Not Found
Server: webserver/1.0
Date: Fri, 05 Nov 2010 08:22:49 GMT
Content-Type: text/html
Content-Length: 112
Connection: close

<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>
<BODY><H4>404 Not Found</H4>
File not found.
</BODY></HTML>

Future Improvements:
- Add HTTPS support (currently only supports HTTP).
- Implement other HTTP methods (e.g., POST, PUT, DELETE).
- Improve logging and debugging features.
- Optimize file caching for better performance.

License:
This project is licensed under the MIT License.

Happy coding! 🚀

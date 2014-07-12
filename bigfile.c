/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Datong Sun
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <time.h>
#include <sys/epoll.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

#define BUFFER_SIZE 4096 // PAGESIZE

typedef struct {
    int fd;
    int header_sent;
} client_ctx;

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: ./bigfile port\nExample: ./bigfile 8888\n");
        return EXIT_SUCCESS;
    }
    close(STDIN_FILENO); // stdin is not needed
    struct sigaction ignore_sig = {
        .sa_handler = SIG_IGN
    };
    if (sigaction(SIGPIPE, &ignore_sig, NULL))
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    int epoll_fd;

    if (listen_fd == -1)
    {
        perror("could not create socket");
        return EXIT_FAILURE;
    }

    int enable = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
    {
        perror("setsockopt SO_REUSEADDR");
        return EXIT_FAILURE;
    }

    struct addrinfo hints = {0}, *res;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(NULL, argv[1], &hints, &res) != 0)
    {
        fprintf(stderr, "getaddrinfo");
        return EXIT_FAILURE;
    }

    if (bind(listen_fd, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("bind failed");
        return EXIT_FAILURE;
    }

    freeaddrinfo(res);

    if (-1 == listen(listen_fd, 10))
    {
        perror("listen failed");
        return EXIT_FAILURE;
    }

    if ((epoll_fd = epoll_create1(EPOLL_CLOEXEC)) < 0)
    {
        perror("epoll_create1");
        return EXIT_FAILURE;
    }

    struct epoll_event ev = {0};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0)
    {
        perror("epoll_ctl listen_fd");
        return EXIT_FAILURE;
    }

    for (;;)
    {
        struct epoll_event events[10]; // this should be enough since we are primarily doing benchmarking
        int n = epoll_wait(epoll_fd, events, sizeof(events) / sizeof(events[0]), -1);
        assert(n);
        if (n <= 0) // timeout would never happen
        {
            perror("epoll_wait");
            return EXIT_FAILURE;
        }

        for (int i = 0; i < n; i++)
        {
            if (events[i].data.fd == listen_fd) // listen fd, sccept new connections
            {
                int client_fd;
                while ((client_fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC)) > 0) {
                    ev.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLET;

                    client_ctx *ctx = malloc(sizeof(client_ctx));
                    if (!ctx)
                    {
                        perror("malloc");
                        return EXIT_FAILURE;
                    }

                    ctx->fd = client_fd;
                    ctx->header_sent = 0;

                    ev.data.ptr = ctx;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0)
                    {
                        perror("epoll_ctl 100");
                        return EXIT_FAILURE;
                    }
                }

                if (client_fd < 0 && errno != EAGAIN)
                {
                    perror("accept4");
                    return EXIT_FAILURE;
                }
            }
            else // client fd
            {
                /*
                 * We do not care about read event at all. I am not trying to implement a HTTP server and
                 * we just assume all requests are valid. This serves the purpose of network
                 * benchmarking and is totally fine. 
                 *
                 * We also do not count how many bytes of data we already sent. Since we provided
                 * the Content-Length header, client will be able to determine when should the
                 * connection be closed. We are always doing passive close. 
                 */
                const char send_buf[BUFFER_SIZE] = {0};
                int n;
                client_ctx *ctx = events[i].data.ptr;

                if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
                {
                    goto close;
                }

#define RESPONSE "HTTP/1.1 200 OK\r\n" \
"Date: Mon, 23 May 2005 22:38:34 GMT\r\n" \
"Bigfile Server (https://github.com/dndx/bigfile_http) / 0.0.1\r\n" \
"Content-Length: 1073741824\r\n" \
"Connection: close\r\n\r\n"

                if (!ctx->header_sent)
                {
                    n = write(ctx->fd, RESPONSE, sizeof(RESPONSE) - 1); // we have to exclude the NULL terminator here
                    if (n != sizeof(RESPONSE) - 1)
                    {
                        fprintf(stderr, "reader partially sent");
                        return EXIT_FAILURE;
                    }
                    ctx->header_sent = 1;
                }

                do
                {
                    n = write(ctx->fd, send_buf, BUFFER_SIZE);
                    if (n == -1)
                    {
                        if (errno == EAGAIN)
                        {
                            break;
                        }
                        else if (errno == EPIPE || errno == ECONNRESET)
                        {
close:
                            if (close(ctx->fd))
                            {
                                perror("close");
                                return EXIT_FAILURE;
                            }
                            free(ctx);
                            break;
                        }
                        else
                        {
                            perror("write");
                            return EXIT_FAILURE;
                        }
                    }
                } while (n == BUFFER_SIZE); // if n is shorter than BUFFER_SIZE, we know we completely filled the kernel send buffer
            }
        }
    }

    // this part is not reachable, but still added those close() just in case
    close(listen_fd);
    close(epoll_fd);
    return EXIT_SUCCESS;  
}

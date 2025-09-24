// minimal_accept.c

#include <stddef.h>
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"

#include <poll.h>

#define MAXFDS 64

int main(void) {
  int srv = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (srv < 0) {
    perror("socket");
    return 1;
  }

  int yes = 1;
  if (setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    perror("setsockopt");
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
  addr.sin_port = htons(8080);              // listen on port 8080

  if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }

  if (listen(srv, 1) < 0) {
    perror("listen");
    return 1;
  }

  printf("Listening on 0.0.0.0:8080 …\n");

  // Setting up poll
  struct pollfd pfds[MAXFDS];
  nfds_t nfds = 0;

  pfds[0].fd = srv;
  pfds[0].events = POLLIN;
  pfds[0].revents = 0;
  nfds = 1;

  // Main event loop
  for (;;) {
    int n = poll(pfds, nfds, -1);
    if (n < 0) {
      perror("poll");
      break;
    }

    short ev = pfds[0].revents;
    pfds[0].revents = 0; // Reset for next poll
    if (ev & POLLIN) {
      // Drain the accept queue
      for (;;) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);

        int conn =
            accept4(srv, (struct sockaddr *)&cli, &cli_len, SOCK_NONBLOCK);
        if (conn < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            break; // queue drained
          perror("accept");
          break;
        }

        char ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip)) == NULL) {
          strcpy(ip, "(invalid)");
        }
        printf("Accepted connection from %s:%u\n", ip,
               (unsigned)ntohs(cli.sin_port));

        // TODO: add `conn` to your poll set or handle it.
        if (nfds == MAXFDS) {
          close(conn);
          continue;
        }
        pfds[nfds].fd = conn;
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        nfds++;
      }
    }

    for (nfds_t i = 1; i < nfds; /* i++ in loop */) {
      short ev = pfds[i].revents;
      pfds[i].revents = 0; // reset for next poll

      if (ev & (POLLERR | POLLHUP | POLLNVAL)) {
        close(pfds[i].fd);
        pfds[i] = pfds[nfds - 1]; // Swap with last
        nfds--;
        continue; // Do not i++ !
      }

      if (ev & POLLIN) {
        char buf[1024];
        ssize_t r = read(pfds[i].fd, buf, sizeof buf);
        hexdump(buf, (size_t)r);
        if (r > 0) {
          (void)write(pfds[i].fd, buf, (size_t)r);
          i++; // keep it
        } else if (r == 0) {
          // EOF, client closed
          close(pfds[i].fd);
          pfds[i] = pfds[nfds - 1];
          nfds--;
        } else {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            i++; // spurious readiness
          } else {
            perror("read");
            close(pfds[i].fd);
            pfds[i] = pfds[nfds - 1];
            nfds--;
          }
        }
      } else {
        i++; // no events for this fd
      }
    }
  }
  return 0;
}

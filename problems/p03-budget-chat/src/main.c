#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"

#define PORT 8080
#define BACKLOG 128

typedef struct Buf {
  uint8_t *data;
  size_t len; // used
  size_t cap; // allocated
} Buf;

typedef struct Conn {
  int fd;
  Buf in;
  Buf out;
  int peer_closed; // 0/1
  int joined;      // 0/1
  char name[16];
  size_t name_len;
  struct Conn *prev, *next;
} Conn;

static Conn *g_conn_head = NULL;
static size_t g_nconn = 0;

static void conn_list_add(Conn *c) {
  c->prev = NULL;
  c->next = g_conn_head;
  if (g_conn_head)
    g_conn_head->prev = c;
  g_conn_head = c;
  g_nconn++;
}

static void conn_list_del(Conn *c) {
  if (c->prev)
    c->prev->next = c->next;
  else
    g_conn_head = c->next;
  if (c->next)
    c->next->prev = c->prev;
  c->prev = c->next = NULL;
  g_nconn--;
}

static Conn *new_conn(int fd) {
  Conn *c = calloc(1, sizeof *c);
  if (!c)
    abort();
  c->fd = fd;
  return c;
}

static void conn_close(Conn *c, int epfd) {
  if (!c)
    return;

  if (c->fd >= 0) {
    // Best-effort remove from epoll
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL) < 0) {
      if (errno != EBADF && errno != ENOENT)
        perror("epoll_ctl DEL");
    }

    // Close, retry on EINTR
    for (;;) {
      if (close(c->fd) == 0)
        break;
      if (errno == EINTR)
        continue;
      if (errno != EBADF)
        perror("close");
      break;
    }
    c->fd = -1;
  }
  conn_list_del(c);
  free(c->in.data);
  c->in.data = NULL;
  c->in.len = c->in.cap = 0;
  free(c->out.data);
  c->out.data = NULL;
  c->out.len = c->out.cap = 0;

  free(c);
}

static int buf_reserve(Buf *b, size_t need) {
  if (b->cap - b->len >= need)
    return 0;
  size_t ncap = b->cap ? b->cap : 4096;
  while (ncap - b->len < need)
    ncap *= 2;
  void *p = realloc(b->data, ncap);
  if (!p)
    return -1;
  b->data = p;
  b->cap = ncap;
  return 0;
}

static int buf_append(Buf *b, const void *src, size_t n) {
  if (buf_reserve(b, n) < 0)
    return -1;
  memcpy(b->data + b->len, src, n);
  b->len += n;
  return 0;
}

static void buf_consume(Buf *b, size_t n) {
  if (n >= b->len) {
    b->len = 0;
    return;
  }
  memmove(b->data, b->data + n, b->len - n);
  b->len -= n;
}

static void conn_send(int epfd, Conn *c, const void *src, size_t n) {
  buf_append(&c->out, src, n);

  // Need to write? enable EPOLLOUT
  if (c->out.len > 0) {
    struct epoll_event ev = {0};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    ev.data.ptr = c;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev) < 0) {
      perror("epoll_ctl MOD");
      conn_close(c, epfd);
      return;
    }
  }
}

static int make_listener(void) {
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  int yes = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
  addr.sin_port = htons(PORT);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  if (listen(fd, BACKLOG) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }
  return fd;
}

static void on_read(Conn *c, int epfd) {
  for (;;) {
    uint8_t tmp[64 * 1024];
    ssize_t n = recv(c->fd, tmp, sizeof(tmp), 0);

    if (n > 0) {
      if (buf_append(&c->in, tmp, n) < 0) {
        perror("realloc");
        exit(EXIT_FAILURE);
      }
      continue; // keep reading in ET mode
    }
    if (n == 0) {         // perr sent FIN
      c->peer_closed = 1; // half-closed; flush pending echo
      break;
    }

    if (errno == EINTR) // retry
      continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) // drained
      break;

    perror("recv");
    conn_close(c, epfd);
    return;
  }

  for (;;) {
    unsigned char *nl = memchr(c->in.data, '\n', c->in.len);
    if (!nl) {
      break;
    }

    size_t raw_len = (size_t)(nl - c->in.data); // Just before \n
    size_t linelen = raw_len;
    if (linelen && c->in.data[linelen - 1] == '\r')
      linelen--; // CRLF

    if (!c->joined) {
      if (linelen > 16) {
        char err[] = "Name too long";
        conn_send(epfd, c, err, sizeof err -1);
        c->peer_closed = 1;
      } else {
        int nameOk = is_alnum_n(c->in.data, linelen);
        if (nameOk) {
          memcpy(&c->name, c->in.data, linelen);
          c->name_len = linelen;
        }

        const char presc_msg[] = "* The room contains: ";
        const char joined_msg_pre[] = "* ";
        const char joined_msg_suf[] = " has entered the room\n";
        conn_send(epfd, c, presc_msg, sizeof presc_msg - 1);
        int first = 1;
        for (Conn *p = g_conn_head; p; p = p->next) {
          if (p == c)
            continue;
          if (!p->joined || p->peer_closed)
            continue;
          if (!first)
            conn_send(epfd, c, ", ", 2);
          conn_send(epfd, c, p->name, p->name_len);

          conn_send(epfd, p, joined_msg_pre, sizeof joined_msg_pre -1);
          conn_send(epfd, p, c->name, c->name_len);
          conn_send(epfd, p, joined_msg_suf, sizeof joined_msg_suf -1);

          first = 0;
        }
        conn_send(epfd, c, "\n", 1);
      }
      c->joined = 1;
    } else {
      // printf("Message: %.*s\n", (int)linelen, (const char *)c->in.data);
      char resp[1024];
      int m = snprintf(resp, sizeof resp, "[%.*s] %.*s\n", (int)c->name_len,
                       c->name, (int)linelen, (const char *)c->in.data);
      size_t n = (m >= (int)sizeof resp) ? sizeof resp - 1 : (size_t)m;
      if (m > 0) {
        for (Conn *p = g_conn_head; p; p = p->next) {
          if (p != c && p->joined)
            conn_send(epfd, p, resp, n);
        }
      }
    }
    buf_consume(&c->in, raw_len + 1); // Include '\n'
  }

  if (c->peer_closed) {
    if (c->joined) {
      for (Conn *p = g_conn_head; p; p = p->next) {
        if (p != c && p->joined){
          conn_send(epfd, p, "* ", 2);
          conn_send(epfd, p, c->name, c->name_len);
          conn_send(epfd, p, " left the room\n", 15);
        }
      }
    }
    if (c->out.len == 0)
      conn_close(c, epfd);
  }
}

static void on_write(Conn *c, int epfd) {
  while (c->out.len) {
    ssize_t n = send(c->fd, c->out.data, c->out.len, MSG_NOSIGNAL);
    if (n > 0) {
      buf_consume(&c->out, (size_t)n);
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      break;
    if (n < 0 && (errno == EPIPE || errno == ECONNRESET)) {
      conn_close(c, epfd);
      return;
    }
    perror("send");
    conn_close(c, epfd);
    return;
  }

  if (c->out.len == 0) {
    if (c->peer_closed) { // finished echoing; mirror the close
      conn_close(c, epfd);
      return;
    }
    struct epoll_event ev = {0}; // stop EPOLLOUT
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.ptr = c;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev) < 0) {
      perror("epoll_ctl MOD");
      conn_close(c, epfd);
    }
  }
}

int main(void) {
#define MAX_EVENTS 10
  struct epoll_event ev, events[MAX_EVENTS];

  printf("Opening listener on port: %d \n", PORT);
  int lfd = make_listener();
  printf("Listening on port: %d \n", PORT);

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = lfd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev) == -1) {
    perror("epoll_ctl: lfd");
    exit(EXIT_FAILURE);
  }

  for (;;) {
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }

    for (int n = 0; n < nfds; ++n) {

      if (events[n].data.fd == lfd) {
        if (events[n].events & (EPOLLERR | EPOLLHUP)) {
          int err = 0;
          socklen_t elen = sizeof(err);
          getsockopt(lfd, SOL_SOCKET, SO_ERROR, &err, &elen);
          fprintf(stderr, "listener error: %s\n", strerror(err));
          exit(1);
        }
        for (;;) {
          struct sockaddr_in cli;
          socklen_t len = sizeof(cli);

          int cfd = accept4(lfd, (struct sockaddr *)&cli, &len,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
          if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) // drained
              break;
            perror("accept");
            break;
          }

          Conn *c = new_conn(cfd);
          ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
          ev.data.ptr = c;
          if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) == -1) {
            perror("epoll_ctl: conn_sock");
            exit(EXIT_FAILURE);
          }
          conn_list_add(c);
          const char welcome_msg[] =
              "Welcome to budgetchat! What shall I call you?\n";
          conn_send(epfd, c, welcome_msg, sizeof welcome_msg -1);
          // Need to write? enable EPOLLOUT
          if (c->out.len > 0) {
            struct epoll_event ev = {0};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
            ev.data.ptr = c;
            if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev) < 0) {
              perror("epoll_ctl MOD");
              conn_close(c, epfd);
              continue;
            }
          }
        }
        continue;
      }

      // client fds
      Conn *c = events[n].data.ptr;
      if (c == NULL)
        continue;
      if (events[n].events & (EPOLLERR)) {
        conn_close(c, epfd);
        continue;
      }

      if (events[n].events & EPOLLIN) {
        on_read(c, epfd);
      }
      if (events[n].events & EPOLLOUT) {
        on_write(c, epfd);
      }
    }
  }

  return 0;
}

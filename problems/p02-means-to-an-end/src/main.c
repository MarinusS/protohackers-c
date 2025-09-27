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

#define PORT 8080
#define BACKLOG 128

typedef struct Tick {
  int32_t ts;
  int32_t price;
} Tick;

typedef struct TickHist {
  Tick *v;
  size_t len, cap;
} TickHist;

static void tickHist_innit(TickHist *h) {
  h->v = NULL;
  h->len = h->cap = 0;
}
static void tickHist_free(TickHist *h) {
  free(h->v);
  h->v = NULL;
  h->len = h->cap = 0;
}

static void tickHist_grow(TickHist *h) {
  if (h->len < h->cap)
    return;
  size_t ncap = h->cap ? 2 * h->cap : 64;
  Tick *nv = realloc(h->v, ncap * sizeof(*nv));
  if (!nv)
    abort();
  h->v = nv;
  h->cap = ncap;
}

static size_t tickHist_lower(const TickHist *h, int32_t ts) {
  size_t lo = 0, hi = h->len;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (h->v[mid].ts < ts)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

static void tickHist_insert(TickHist *h, int32_t ts, int32_t price) {
  tickHist_grow(h);
  size_t i = tickHist_lower(h, ts);

  if (i < h->len && h->v[i].ts == ts) {
    // Undefined behaviour but overwrite seems sane
    h->v[i].price = price;
    return;
  }

  memmove(h->v + i + 1, h->v + i, (h->len - i) * sizeof *h->v);
  h->v[i].ts = ts;
  h->v[i].price = price;
  h->len++;
}

static int32_t tickHist_mean(const TickHist *h, int32_t ts_min,
                             int32_t ts_max) {
  if (ts_min > ts_max || h->len == 0)
    return 0;

  size_t i = tickHist_lower(h, ts_min);
  int64_t sum = 0;
  size_t cnt = 0;
  while (i < h->len && h->v[i].ts <= ts_max) {
    sum += h->v[i].price;
    cnt++;
    i++;
  }
  return cnt ? (int32_t)(sum / (int64_t)cnt) : 0;
}

typedef struct Buf {
  uint8_t *data;
  size_t len; // used
  size_t cap; // allocated
} Buf;

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

typedef struct Conn {
  int fd;
  Buf in;
  Buf out;
  int peer_closed; // 0/1
  TickHist tickHist;
} Conn;

static Conn *new_conn(int fd) {
  Conn *c = calloc(1, sizeof *c);
  if (!c)
    abort();
  c->fd = fd;
  tickHist_innit(&c->tickHist);
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

  free(c->in.data);
  c->in.data = NULL;
  c->in.len = c->in.cap = 0;
  free(c->out.data);
  c->out.data = NULL;
  c->out.len = c->out.cap = 0;
  tickHist_free(&c->tickHist);

  free(c);
}

static inline int32_t be_i32(const uint8_t *p) {
  uint32_t u;
  memcpy(&u, p, 4);  // avoid alignment/aliasing issues
  u = ntohl(u);      // network(big) -> host
  return (int32_t)u; // two's-complement re-interpretation
}

static inline void be_put_i32(uint8_t out[4], int32_t x) {
  uint32_t u = htonl((uint32_t)x);
  out[3] = (uint8_t)(u >> 24);
  out[2] = (uint8_t)(u >> 16);
  out[1] = (uint8_t)(u >> 8);
  out[0] = (uint8_t)(u);
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
      c->peer_closed = 1; // half-closed; flush pending
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

  // Handle the input buffer
  // printf("In c input buffer:\n");
  // hexdump(c->in.data, c->in.len);
  //  Process message by message, they are 9 bytes long each exactly
  while (c->in.len >= 9) {
    uint8_t *data = c->in.data;

    if (data[0] == 'I') {
      int32_t ts = be_i32(data + 1);
      int32_t price = be_i32(data + 5);
      tickHist_insert(&c->tickHist, ts, price);
      //printf("Insert: ts=%d | price=%d\n", ts, price);
    } else if (data[0] == 'Q') {
      int32_t ts_min = be_i32(data + 1);
      int32_t ts_max = be_i32(data + 5);
      int32_t mean = tickHist_mean(&c->tickHist, ts_min, ts_max);
      //printf("Query: ts_min=%d | ts_max=%d | mean=%d\n", ts_min, ts_max, mean);
      uint8_t out[4];
      be_put_i32(out, mean);
      buf_append(&c->out, out, 4);
    }

    buf_consume(&c->in, 9);
  }

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

  if (c->peer_closed && c->out.len == 0)
    conn_close(c, epfd);
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
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            perror("accept");
            break;
          }

          Conn *c = new_conn(cfd);
          ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
          ev.data.fd = cfd;
          ev.data.ptr = c;
          if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) == -1) {
            perror("epoll_ctl: conn_sock");
            exit(EXIT_FAILURE);
          }
        }
        continue;
      }

      // client fds
      Conn *c = events[n].data.ptr;
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

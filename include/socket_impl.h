#ifndef SOCKET_IMPL_H_
#define SOCKET_IMPL_H_

#include <mobile.h>

bool impl_sock_open(void* user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport);
void impl_sock_close(void* user, unsigned conn);
int socket_impl_connect(void* user, unsigned conn, const struct mobile_addr *addr);
bool socket_impl_listen(void* user, unsigned conn);
bool socket_impl_accept(void* user, unsigned conn);
int socket_impl_send(void* user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr);
int socket_impl_recv(void* user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr);

#endif /* SOCKET_IMPL_H_ */

#include "src/CrabitBuildProfile.h"
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
#include <stdint.h>
#include <sys/types.h>
#include <lwip/sockets.h>
#include <errno.h>

// Keep the SDK's C socket types/macros out of Arduino Client's C++ interface.
// Observe only: never allocate, close, bind or redirect a socket.
int findNtpSocket(uint16_t port) {
    const int savedErrno = errno;
    int found = -1;
    for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + MEMP_NUM_NETCONN; ++fd) {
        struct sockaddr_in local = {0};
        socklen_t length = sizeof(local);
        if (lwip_getsockname(fd, (struct sockaddr*)&local, &length) == 0 &&
                local.sin_family == AF_INET && ntohs(local.sin_port) == port) {
            found = fd; break;
        }
    }
    errno = savedErrno;
    return found;
}
#endif

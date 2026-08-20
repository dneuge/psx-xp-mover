#define MAJOR_WINSOCK_VERSION 2
#define MINOR_WINSOCK_VERSION 2

bool initialize_os_network_apis() {
    unsigned long res = 0;

    WSADATA wsadata = {0,};

    uint16_t requested_winsock_version = (MAJOR_WINSOCK_VERSION << 8) | MINOR_WINSOCK_VERSION;
    res = WSAStartup(requested_winsock_version, &wsadata);
    if (res) {
        printf("[XPMover] WSAStartup failed, res=%lu\n", res);
        return false;
    }

    return true;
}

void close_network_socket(socket_t sd) {
    // Microsoft API docs:
    // https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/winsock2/nf-winsock2-closesocket.md
    closesocket(sd);
}


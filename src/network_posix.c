bool initialize_os_network_apis() {
    // nothing to do for this target system
    return true;
}

void close_network_socket(socket_t sd) {
    close(sd);
}

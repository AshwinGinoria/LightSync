#include "udp_server.hpp"
#include "logger.hpp"
#include <iostream>

UDP_Server::UDP_Server(int port, std::string ip_address) {
#ifdef _WIN32
    WSADATA wsaData;
    int startup_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startup_result != 0) {
        LOGGER.error("WSAStartup failed: {}", startup_result);
        throw std::runtime_error("WSAStartup failed");
    }
#endif

    LOGGER.debug("Creating socket");

    this->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (this->sock < 0) {
        LOGGER.error("Error creating socket");
        throw std::runtime_error("Error creating socket");
    }

    LOGGER.debug("Preparing to send data to server at {}:{}",
                 ip_address.c_str(), port);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);

#ifdef _WIN32
    inet_pton(AF_INET, ip_address.c_str(), &server_address.sin_addr);
#else
    server_address.sin_addr.s_addr = inet_addr(ip_address.c_str());
#endif

    LOGGER.info("Ready to send data to server at {}:{}", ip_address.c_str(),
                port);
}

void UDP_Server::send_message(std::vector<uint8_t> *message) {
    LOGGER.debug("Sending message of length: {}", message->size());

    sendto(sock, reinterpret_cast<const char *>(message->data()),
           message->size(), 0, (struct sockaddr *)&server_address,
           sizeof(server_address));
}

void UDP_Server::stop() {
    LOGGER.debug("Closing socket");

#ifdef _WIN32
    int result = closesocket(sock);
    WSACleanup();
#else
    int result = close(sock);
#endif

    if (result == -1)
        LOGGER.error("Error closing socket");
    else
        LOGGER.info("Socket closed!");
}

UDP_Server::~UDP_Server() {
    this->stop();
}

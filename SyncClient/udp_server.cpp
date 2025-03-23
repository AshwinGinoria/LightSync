#include <iostream>
#include "udp_server.hpp"
#include "logger.hpp"

void UDP_Server::stop()
{
    LOGGER.debug("Closing socket");

    if (close(sock) == -1)
        LOGGER.error("Error closing socket");
    else
        LOGGER.info("Socket closed!");
}

// Create a server
UDP_Server::UDP_Server(int port, std::string ip_address) : sock(socket(AF_INET, SOCK_DGRAM, 0))
{
    LOGGER.debug("Creating socket");

    // SOCK_DGRAM for UDP
    if (this->sock == -1)
    {
        LOGGER.error("Error creating socket");
        throw std::runtime_error("Error creating socket");
    }

    LOGGER.debug("Preparing to send data to server at {}:{}", ip_address.c_str(), port);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr(ip_address.c_str());

    LOGGER.info("Ready to send data to server at {}:{}", ip_address.c_str(), port);
}

// Send a message to the server
void UDP_Server::send_message(std::vector<uint8_t> *message)
{
    LOGGER.debug("Sending message: {}", *message);
    sendto(sock, message->data(), message->size(), 0, (struct sockaddr *)&server_address, sizeof(server_address));
}

UDP_Server::~UDP_Server()
{
    this->stop();
}

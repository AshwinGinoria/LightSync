#pragma once
#include <string>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <vector>
#include "logger.cpp"

/* ------------------ Interface ------------------ */

class UDP_Server
{
private:
    int sock;                          // Socket
    Logger &logger;                    // Logger
    struct sockaddr_in server_address; // UDP Server ip addr / port

public:
    UDP_Server(int, std::string);              // Create a UDP Server
    void stop();                               // Stop the server
    void send_message(std::vector<uint8_t> *); // Send a message to the server
    ~UDP_Server();
};

/* ------------------ Implementation ------------------ */

// Stop the server
void UDP_Server::stop()
{
    logger.debug("Closing socket");

    if (close(sock) == -1)
        logger.error("Error closing socket");
    else
        logger.info("Socket closed!");
}

// Create a server
UDP_Server::UDP_Server(int port, std::string ip_address) : logger(Logger::getInstance()), sock(socket(AF_INET, SOCK_DGRAM, 0))
{
    logger.debug("Creating socket");

    // SOCK_DGRAM for UDP
    if (this->sock == -1)
    {
        logger.error("Error creating socket");
        throw std::runtime_error("Error creating socket");
    }

    logger.debug("Preparing to send data to server at {}:{}", ip_address.c_str(), port);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr(ip_address.c_str());

    logger.info("Ready to send data to server at {}:{}", ip_address.c_str(), port);
}

// Send a message to the server
void UDP_Server::send_message(std::vector<uint8_t> *message)
{
    logger.debug("Sending message: {}", *message);
    sendto(sock, message->data(), message->size(), 0, (struct sockaddr *)&server_address, sizeof(server_address));
}

UDP_Server::~UDP_Server()
{
    this->stop();
}

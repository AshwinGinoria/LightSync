#pragma once
#include <string>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>

/* ------------------ Interface ------------------ */

class UDP_Server
{
private:
    int sock;                          // Socket
    struct sockaddr_in server_address; // UDP Server ip addr / port

public:
    UDP_Server(int, std::string);              // Create a UDP Server
    void stop();                               // Stop the server
    void send_message(std::vector<uint8_t> *); // Send a message to the server
    ~UDP_Server();
};

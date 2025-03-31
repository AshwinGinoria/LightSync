#pragma once
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

class UDP_Server {
  private:
#ifdef _WIN32
    SOCKET sock;
#else
    int sock;
#endif
    struct sockaddr_in server_address;

  public:
    UDP_Server(int port, std::string ip_address);
    void stop();
    void send_message(std::vector<uint8_t> *message);
    ~UDP_Server();
};

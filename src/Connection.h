#pragma once

#include <string>

struct Connection {
    int id;
    int client_fd = -1;
    int server_fd = -1;

    std::string client_addr;
    std::string server_addr;

    std::string client_out;
    std::string server_out;
    bool closed = false;
};

enum class FdRole {
    LISTENER,
    CLIENT,
    SERVER
};

struct FdContext {
    Connection* conn;
    FdRole role;
};

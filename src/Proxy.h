#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "Connection.h"
#include "ProtocolInterceptor.h"

class Proxy {

public:
    Proxy(std::string& lst_host, uint16_t listen_port, std::string& db_host, uint16_t dbs_port);

    void setInterceptor(std::unique_ptr<IProtocolInterceptor> interceptor);
    bool init();
    void run();

private:
    std::unique_ptr<IProtocolInterceptor> interceptor_;
    std::string lst_host_;
    uint16_t lst_port_;
    std::string dbs_host_;
    uint16_t dbs_port_;



    int next_connection_id_ = 1;
    int epoll_fd_   = -1;
    int listener_fd_ = -1;

    //  All connections lives here
    std::vector<std::unique_ptr<Connection>> connections_;

    //  FdContext for every Fd by key 
    std::map<int, FdContext> fd_context_map_;

    bool setup_listener();
    bool setup_epoll();

    void handle_listener_event(uint32_t events);
    void handle_socket_event(struct epoll_event& ev);

    int  connect_to_db();
    void close_connection(Connection* conn);
    void update_epoll_events(int fd, FdContext* context, bool want_read, bool want_write);
    bool add_fd_to_epoll(int fd, FdContext* context, uint32_t events);
};

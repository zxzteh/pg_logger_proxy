#include "Proxy.h"

//  Set new flag for nonblocking mode 
static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return false;
    return true;
}

//  move data from buffer to socket
static int flush_buffer(int fd, std::string& buf) {
    while (!buf.empty()) {
        ssize_t n = ::send(fd, buf.data(), buf.size(), 0);

        if (n > 0) {
            buf.erase(0, static_cast<size_t>(n));
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0; // socket not ready
        }

        return -1;  // error 
    }

    return 0;
}


Proxy::Proxy(std::string& lst_host, uint16_t lst_port, std::string& dbs_host, uint16_t dbs_port)  
    : lst_host_(lst_host)
    , lst_port_(lst_port)
    , dbs_host_(dbs_host)
    , dbs_port_(dbs_port) {}

bool Proxy::setup_listener() {

    //  IPv4 TCP default fd init
    listener_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd_ == -1) return false;

    //  Socket behavior: allow reuse addr, to prevent errors 
    int enable = 1;
    if (setsockopt(listener_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) return false;

    //  Prepare addr before bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(lst_port_);
    if (inet_pton(AF_INET, lst_host_.c_str(), &addr.sin_addr) <= 0) return false;

    //  Bind addr to socket
    if (bind(listener_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) return false;

    //  To feel free to rotate poll eventloop
    if (!set_nonblocking(listener_fd_)) return false;

    //  To make listener listen, server case
    if (listen(listener_fd_, SOMAXCONN) == -1) return false;
    
    std::cout << "LISTEN: " << lst_host_ << ":" << lst_port_ << "\n"
              << "FRWARD: " << dbs_host_ << ":" << dbs_port_ << "\n";

    return true;
}

bool Proxy::setup_epoll() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) return false;
    
    FdContext listener_context;
    listener_context.role = FdRole::LISTENER;
    listener_context.conn = nullptr;

    fd_context_map_[listener_fd_] = listener_context;

    //  Event when client sends data to proxy
    if (!add_fd_to_epoll(listener_fd_, &fd_context_map_[listener_fd_], EPOLLIN)) {
        return false;
    }

    return true;
}

bool Proxy::init() {
    if (!setup_listener()) return false;
    if (!setup_epoll()) return false;
    return true;
}

int Proxy::connect_to_db() {
    //  IPv4 TCP default fd init
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return -1;

    //  Close in case of error is neccessary, cause fd amount can be huge
    //  And we don't want dead fd 

    if (!set_nonblocking(fd)) {
        close(fd);
        return -1;
    }

    //  Prepare addr before connect
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dbs_port_);
    if (inet_pton(AF_INET, dbs_host_.c_str(), &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    
    int res = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (res == -1 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    return fd;
}

void Proxy::close_connection(Connection* conn) {
    if (!conn || conn->closed) return;
    conn->closed = true;

    //  Close client
    if (conn->client_fd != -1) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn->client_fd, nullptr);
        close(conn->client_fd);
        fd_context_map_.erase(conn->client_fd);
        conn->client_fd = -1;
    }

    //  Close server
    if (conn->server_fd != -1) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn->server_fd, nullptr);
        close(conn->server_fd);
        fd_context_map_.erase(conn->server_fd);
        conn->server_fd = -1;
    }
}

bool Proxy::add_fd_to_epoll(int fd, FdContext* context, uint32_t events) {
    epoll_event ev{};
    ev.data.ptr = context;
    ev.events = events;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) return false;
    return true;
}

void Proxy::update_epoll_events(int fd, FdContext* context, bool want_read, bool want_write) {
    if (fd == -1) return;

    uint32_t new_events = EPOLLRDHUP;
    if (want_read) new_events |= EPOLLIN;
    if (want_write) new_events |= EPOLLOUT;

    epoll_event ev{};
    ev.data.ptr = context;
    ev.events = new_events;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        if (errno == ENOENT) {
            add_fd_to_epoll(fd, context, ev.events);
        }
    }
}

void Proxy::handle_listener_event(uint32_t events) {
    if (!(events & EPOLLIN)) return;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(listener_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; //  No new connections
            } else {
                perror("accept");
                break;
            }
        }

        if (!set_nonblocking(client_fd)) {
            perror("set_nonblocking client");
            close(client_fd);
            continue;  //  try again
        }

        int server_fd = connect_to_db();
        if (server_fd == -1) {
            close(client_fd);
            continue;  //  try again
        }

        auto conn = std::make_unique<Connection>();
        conn->id = next_connection_id_++;

        //  Add addr
        char addrbuf[64];
        inet_ntop(AF_INET, &client_addr.sin_addr, addrbuf, sizeof(addrbuf));
        uint16_t client_port = ntohs(client_addr.sin_port);
        conn->client_addr = std::string(addrbuf) + ":" + std::to_string(client_port);
        conn->server_addr = dbs_host_ + ":" + std::to_string(dbs_port_);
        
        //  Add fd
        conn->client_fd = client_fd;
        conn->server_fd = server_fd;
        Connection* conn_ptr = conn.get();
        connections_.push_back(std::move(conn));

        std::cout << "New link: client_fd=" << client_fd << " server_fd=" << server_fd << "\n";

        //  Add context
        fd_context_map_[client_fd] = FdContext{ conn_ptr, FdRole::CLIENT };
        fd_context_map_[server_fd] = FdContext{ conn_ptr, FdRole::SERVER };

        //  Now we wait events on this link, 
        //  We don't need EPOLLOUT on client right now
        add_fd_to_epoll(client_fd, &fd_context_map_[client_fd], EPOLLIN | EPOLLRDHUP);
        add_fd_to_epoll(server_fd, &fd_context_map_[server_fd], EPOLLIN | EPOLLOUT | EPOLLRDHUP);
    }
}

void Proxy::handle_socket_event(struct epoll_event& ev) {

    auto* context = static_cast<FdContext*>(ev.data.ptr);
    if (!context || !context->conn) return;
    Connection* conn = context->conn;

    bool is_client = (context->role == FdRole::CLIENT);
    int fd = is_client ? conn->client_fd : conn->server_fd;

    if (fd == -1) {
        close_connection(conn);
        return;
    }

    //  Close event
    if (ev.events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
        std::cerr << "Close fd=" << fd << " role=" << (is_client ? "client" : "server") << "\n";
        close_connection(conn);
        return;
    }

    // Write to socket event
    if (ev.events & EPOLLOUT) {
        std::string& out_buf = is_client ? conn->client_out : conn->server_out;
        if (!out_buf.empty()) {
            if (flush_buffer(fd, out_buf) == -1) {
                close_connection(conn);
                return;
            }
        }
    }

    // Read from socket event
    if (ev.events & EPOLLIN) {
        char buf[8192];
        ssize_t n = 0;

        while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {

            std::size_t sz = static_cast<std::size_t>(n);

            //  Interceptor GO 
            if (interceptor_) {
                if (is_client) {
                    interceptor_->onClientData(*conn, buf, sz);
                } else {
                    interceptor_->onServerData(*conn, buf, sz);
                } 
            }

            //  Routing
            (is_client ? conn->server_out : conn->client_out).append(buf, sz);
        }

        //  Connection closed
        if (n == 0) {
            std::cerr << "Received EOF on fd=" << fd << " role=" << (is_client ? "client" : "server") << "\n";
            close_connection(conn);
            return;
        }

        //  Receive error
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recv");
            close_connection(conn);
            return;
        }
    }


    // Refresh EPOLLOUT if we have smthng to write
    auto it_client = fd_context_map_.find(conn->client_fd);
    if (it_client != fd_context_map_.end()) {
        bool want_write_client = !conn->client_out.empty();
        update_epoll_events(conn->client_fd, &it_client->second, true, want_write_client);
    }

    auto it_server = fd_context_map_.find(conn->server_fd);
    if (it_server != fd_context_map_.end()) {
        bool want_write_server = !conn->server_out.empty();
        update_epoll_events(conn->server_fd, &it_server->second, true, want_write_server);
    }
}

void Proxy::run() {
    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (true) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;  //  Interrupted by signal, just continue
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            auto* context = static_cast<FdContext*>(events[i].data.ptr);
            if (!context) continue;

            if (context->role == FdRole::LISTENER) {
                handle_listener_event(events[i].events);
            } else {
                handle_socket_event(events[i]);
            }
        }
    }
}

void Proxy::setInterceptor(std::unique_ptr<IProtocolInterceptor> interceptor) {
    interceptor_ = std::move(interceptor);
}

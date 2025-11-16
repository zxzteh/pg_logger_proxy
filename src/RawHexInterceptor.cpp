#include "RawHexInterceptor.h"

RawHexInterceptor::RawHexInterceptor(const std::string& path) {
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_) {
        std::cerr << "Failed to open hex dump file: " << path << "\n";
    }
}

void RawHexInterceptor::dumpLine(const char* direction,
                                 const Connection& conn,
                                 const char* data,
                                 std::size_t len) {
    if (!file_) return;

    file_ << std::left  << std::setw(4)  << direction
          << " client=" << std::setw(22) << conn.client_addr
          << " server=" << std::setw(22) << conn.server_addr
          << " cfd="    << std::right << std::setw(3) << conn.client_fd
          << " sfd="    << std::right << std::setw(3) << conn.server_fd
          << " len="    << std::right << std::setw(5) << len
          << " hex=";

    file_ << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; i++) {
        unsigned byte = static_cast<unsigned char>(data[i]);
        file_ << std::setw(2) << byte;
    }

    file_ << std::dec << std::setfill(' ') << "\n";
    file_.flush();
}


void RawHexInterceptor::onClientData(Connection& conn, const char* data, std::size_t len) {
    dumpLine("C->S", conn, data, len);
}

void RawHexInterceptor::onServerData(Connection& conn, const char* data, std::size_t len) {
    dumpLine("S->C", conn, data, len);
}

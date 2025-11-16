#pragma once

#include "ProtocolInterceptor.h"
#include <fstream>
#include <string>
#include <iomanip>
#include <iostream>

//  Get stream -> dump hex stream with extra info
//  Was used in debugging 

class RawHexInterceptor : public IProtocolInterceptor {
public:
    explicit RawHexInterceptor(const std::string& path);
    
    void onClientData(Connection& conn, const char* data, std::size_t len) override;

    void onServerData(Connection& conn, const char* data, std::size_t len) override;

private:
    std::ofstream file_;

    void dumpLine(const char* direction, const Connection& conn, const char* data, std::size_t len);
};

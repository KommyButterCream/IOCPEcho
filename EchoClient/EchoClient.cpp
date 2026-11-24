#include "../Common/EchoProtocol.h"

#include "IOCPNetwork.h"

#include <iostream> // for std::cin, std::cout
#include <string>   // for std::string, std::getline
int main()
{
    std::cout << "EchoClient\n";
    std::cout << "IOCPNetwork version: "
        << IOCPNetwork::GetLibraryVersion()
        << '\n';

    std::cout << "Echo packet ID: " << IOCPEcho::EchoPacketId << '\n';

    std::cout << "Press Enter to exit.\n";

    std::string input;
    std::getline(std::cin, input);

    return 0;
}
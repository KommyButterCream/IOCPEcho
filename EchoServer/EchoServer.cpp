#include "../Common/EchoProtocol.h"

#include "ClientSession.h"
#include "IOCPServer.h"

#include <chrono>    // for std::chrono::hours
#include <cstddef>   // for std::size_t
#include <exception> // for std::exception
#include <iostream>  // for std::cin, std::cout, std::cerr
#include <string>    // for std::string, std::getline
#include <syncstream> // for std::osyncstream
#include <utility>   // for std::move

namespace
{
	// 중단점에서 오래 대기할 때 활성화하고, 일반 실행 검증 시 false로 바꾼다.
	constexpr bool EnableDebugTimeouts = true;
	constexpr std::chrono::hours DebugTimeout{ 1 };

	void OnConnected(IOCPNetwork::ClientSession& session)
	{
		const auto id = session.GetConnectionId();

		std::osyncstream{ std::cout }
			<< "[Connected] index=" << id.index
			<< ", generation=" << id.generation
			<< "\n";
	}

	void OnDisconnected(IOCPNetwork::ClientSession& session, IOCPNetwork::DisconnectInfo info)
	{
		const auto id = session.GetConnectionId();

		std::osyncstream{ std::cout }
			<< "[Disconnected] index=" << id.index
			<< ", generation=" << id.generation
			<< ", reason=" << static_cast<int>(info.reason)
			<< ", nativeError=" << info.nativeError
			<< ", shutdownTimeout=" << info.shutdownTimedOut
			<< "\n";
	}

	void OnEchoPacket(IOCPNetwork::ClientSession& session, IOCPNetwork::Packet packet)
	{
		const auto id = session.GetConnectionId();
		const std::size_t payloadSize = packet.View().payload.size();

		const auto result = session.Send(std::move(packet));

		if (result != IOCPNetwork::SendResult::Queued)
		{
			std::osyncstream{ std::cout }
				<< "[Echo rejected] index=" << id.index
				<< ", result=" << static_cast<int>(result)
				<< "\n";

			return;
		}

		std::osyncstream{ std::cout }
			<< "[Echo queued] index=" << id.index
			<< ", payloadBytes=" << payloadSize
			<< "\n";
	}

	int RunServer()
	{
		IOCPNetwork::IOCPServer server;

		IOCPNetwork::ConnectionEvents events;
		events.connected = OnConnected;
		events.disconnected = OnDisconnected;

		if (!server.SetConnectionEvents(std::move(events)))
		{
			std::osyncstream{ std::cerr }
			<< "Failed to register connection events.\n";

			return 1;
		}

		const auto registration = server.RegisterPacketHandler(
			IOCPEcho::EchoPacketId, OnEchoPacket);

		if (registration != IOCPNetwork::HandlerRegistrationResult::Registered)
		{
			std::osyncstream{ std::cerr }
				<< "Failed to register echo handler. result="
				<< static_cast<int>(registration)
				<< '\n';

			return 1;
		}

		IOCPNetwork::ServerOptions options;
		options.bindAddress = "127.0.0.1";
		options.port = 9000;
		options.maxConnections = 16;
		options.network.ioWorkerCount = 2;
		options.network.jobWorkerCount = 2;
		options.network.maxPacketPoolBytes = 16 * 1024 * 1024;

		if (EnableDebugTimeouts)
		{
			options.network.handshakeTimeout = DebugTimeout;
			options.network.heartbeatTimeout = DebugTimeout;
			options.network.receiveTimeout = DebugTimeout * 2;
			options.network.backpressureTimeout = DebugTimeout;
			options.network.shutdownTimeout = DebugTimeout;
		}

		const auto startResult = server.Start(options);

		if (!startResult)
		{
			std::osyncstream{ std::cerr }
				<< "Failed to start server. error="
				<< static_cast<int>(startResult.error)
				<< ", nativeError=" << startResult.nativeError
				<< '\n';

			return 1;
		}

		std::osyncstream{ std::cout }
			<< "EchoServer listening on "
			<< options.bindAddress << ':' << options.port
			<< "\nPress Enter to stop.\n";

		std::string input;
		std::getline(std::cin, input);

		std::osyncstream{ std::cout } << "Stopping EchoServer...\n";

		const auto stopResult = server.Stop();

		if (stopResult != IOCPNetwork::StopResult::Stopped &&
			stopResult != IOCPNetwork::StopResult::AlreadyStopped)
		{
			std::osyncstream{ std::cout }
				<< "Failed to stop server. result="
				<< static_cast<int>(stopResult)
				<< '\n';

			return 1;
		}

		std::osyncstream{ std::cout } << "EchoServer stopped.\n";

		return 0;
	}
}

int main()
{
	try
	{
		return RunServer();
	}
	catch (const std::exception& exception)
	{
		std::osyncstream{ std::cerr }
			<< "EchoServer exception: "
			<< exception.what()
			<< '\n';

		return 1;
	}

}
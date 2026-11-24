#include "../Common/EchoProtocol.h"

#include "ClientSession.h"
#include "IOCPClient.h"

#include <chrono>             // for std::chrono::seconds
#include <condition_variable> // for std::condition_variable
#include <exception>          // for std::exception
#include <iostream>           // for std::cout, std::cerr
#include <mutex>              // for std::mutex, std::lock_guard, std::unique_lock
#include <span>               // for std::span, std::as_bytes
#include <string>             // for std::string
#include <syncstream>         // for std::osyncstream
#include <utility>            // for std::move

namespace
{
	struct ClientState
	{
		std::mutex mutex;
		std::condition_variable changed;

		IOCPNetwork::ConnectionId connectionId{};

		bool connected = false;
		bool ended = false;
		bool responseReceived = false;

		std::string response;
	};

	bool RegisterCallbacks(IOCPNetwork::IOCPClient& client, ClientState& state)
	{
		IOCPNetwork::ConnectionEvents events;

		events.connected = [&state](IOCPNetwork::ClientSession& session)
			{
				const auto id = session.GetConnectionId();

				{
					std::lock_guard<std::mutex> lock{ state.mutex };

					state.connectionId = id;
					state.connected = true;
				}

				state.changed.notify_one();

				std::osyncstream{ std::cout }
					<< "[Connected] index=" << id.index
					<< ", generation=" << id.generation
					<< '\n';
			};

		events.disconnected = [&state](IOCPNetwork::ClientSession& session, IOCPNetwork::DisconnectInfo info)
			{
				const auto id = session.GetConnectionId();

				{
					std::lock_guard<std::mutex> lock{ state.mutex };

					state.connected = false;
					state.ended = true;
				}

				state.changed.notify_one();

				std::osyncstream{ std::cout }
					<< "[Disconnected] index=" << id.index
					<< ", reason=" << static_cast<int>(info.reason)
					<< ", nativeError=" << info.nativeError
					<< ", shutdownTimedOut=" << info.shutdownTimedOut
					<< '\n';
			};

		events.connectFailed = [&state](IOCPNetwork::DisconnectInfo info)
			{
				{
					std::lock_guard<std::mutex> lock{ state.mutex };

					state.ended = true;
				}

				state.changed.notify_one();

				std::osyncstream{ std::cerr }
					<< "[Connect failed] reason="
					<< static_cast<int>(info.reason)
					<< ", nativeError=" << info.nativeError
					<< '\n';
			};

		if (!client.SetConnectionEvents(std::move(events)))
		{
			std::osyncstream{ std::cerr } << "Failed to register connection events.\n";

			return false;
		}

		const auto registration = client.RegisterPacketHandler(
			IOCPEcho::EchoPacketId, [&state](IOCPNetwork::ClientSession&, IOCPNetwork::Packet packet)
			{
				const auto payload = packet.View().payload;

				std::string response;

				if (!payload.empty())
				{
					response.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
				}

				{
					std::lock_guard<std::mutex> lock{ state.mutex };

					state.response = std::move(response);
					state.responseReceived = true;
				}

				state.changed.notify_one();
			});

		if (registration != IOCPNetwork::HandlerRegistrationResult::Registered)
		{
			std::osyncstream{ std::cerr }
				<< "Failed to register echo handler. result="
				<< static_cast<int>(registration)
				<< '\n';

			return false;
		}

		return true;
	}

	bool ExchangeEcho(IOCPNetwork::IOCPClient& client, ClientState& state)
	{
		IOCPNetwork::ConnectionId connectionId{};

		{
			std::unique_lock<std::mutex> lock{ state.mutex };

			const bool signaled = state.changed.wait_for(
				lock,
				std::chrono::seconds{ 15 },
				[&state]
				{
					return state.connected || state.ended;
				});

			if (!signaled || state.ended)
			{
				std::osyncstream{ std::cerr } << "Connection was not ready.\n";

				return false;
			}

			connectionId = state.connectionId;
		}

		const std::string message = "Hello, IOCP!";

		const auto payload = std::as_bytes(std::span<const char>{message.data(), message.size()});

		auto packetResult = client.CreatePacket(IOCPEcho::EchoPacketId, payload);

		if (!packetResult)
		{
			std::osyncstream{ std::cerr }
				<< "Failed to create packet. error="
				<< static_cast<int>(packetResult.error)
				<< '\n';

			return false;
		}

		const auto sendResult = client.Send(connectionId, std::move(packetResult.packet));

		if (sendResult != IOCPNetwork::SendResult::Queued)
		{
			std::osyncstream{ std::cerr }
				<< "Send rejected. result="
				<< static_cast<int>(sendResult)
				<< '\n';

			return false;
		}

		std::osyncstream{ std::cout } << "[Send queued] " << message << '\n';

		std::string response;

		{
			std::unique_lock<std::mutex> lock{ state.mutex };

			const bool signaled = state.changed.wait_for(
				lock,
				std::chrono::seconds{ 15 },
				[&state]
				{
					return state.responseReceived || state.ended;
				});

			if (!signaled || !state.responseReceived)
			{
				std::osyncstream{ std::cerr } << "Echo response was not received.\n";

				return false;
			}

			response = state.response;
		}

		std::osyncstream{ std::cout } << "[Echo received] " << response << '\n';

		if (response != message)
		{
			std::osyncstream{ std::cerr } << "Echo response mismatch.\n";

			return false;
		}

		std::osyncstream{ std::cout } << "Echo verification passed.\n";

		return true;
	}

	int RunClient()
	{
		ClientState state;
		IOCPNetwork::IOCPClient client;

		if (!RegisterCallbacks(client, state))
		{
			return 1;
		}

		IOCPNetwork::ClientOptions options;
		options.serverAddress = "127.0.0.1";
		options.port = 9000;
		options.network.ioWorkerCount = 2;
		options.network.jobWorkerCount = 2;
		options.network.maxPacketPoolBytes = 16 * 1024 * 1024;

		const auto startResult = client.Start(options);

		if (!startResult)
		{
			std::osyncstream{ std::cerr }
				<< "Failed to start client. error="
				<< static_cast<int>(startResult.error)
				<< ", nativeError=" << startResult.nativeError
				<< '\n';

			return 1;
		}

		const bool succeeded = ExchangeEcho(client, state);

		std::osyncstream{ std::cout } << "Stopping EchoClient...\n";

		const auto stopResult = client.Stop();

		if (stopResult != IOCPNetwork::StopResult::Stopped &&
			stopResult != IOCPNetwork::StopResult::AlreadyStopped)
		{
			std::osyncstream{ std::cerr }
				<< "Failed to stop client. result="
				<< static_cast<int>(stopResult)
				<< '\n';

			return 1;
		}

		std::osyncstream{ std::cout } << "EchoClient stopped.\n";

		return succeeded ? 0 : 1;
	}

}

int main()
{
	try
	{
		return RunClient();
	}
	catch (const std::exception& exception)
	{
		std::osyncstream{ std::cerr }
			<< "EchoClient exception: "
			<< exception.what()
			<< '\n';

		return 1;
	}
}
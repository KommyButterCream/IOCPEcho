#include "../Common/EchoProtocol.h"

#include "ClientSession.h"
#include "IOCPClient.h"

#include <chrono>             // for std::chrono::hours, std::chrono::milliseconds
#include <condition_variable> // for std::condition_variable
#include <cstddef>            // for std::size_t
#include <exception>          // for std::exception
#include <iostream>           // for std::cin, std::cout, std::cerr
#include <mutex>              // for std::mutex, std::lock_guard, std::unique_lock
#include <span>               // for std::span, std::as_bytes
#include <string>             // for std::string, std::to_string, std::getline
#include <syncstream>         // for std::osyncstream
#include <utility>            // for std::move
#include <vector>             // for std::vector

namespace
{
	// 중단점에서 오래 대기할 때 활성화하고, 일반 실행 검증 시 false로 바꾼다.
	constexpr bool EnableDebugTimeouts = false;
	constexpr std::chrono::hours DebugTimeout{ 2 };

	struct ClientState
	{
		std::mutex mutex;
		std::condition_variable changed;

		IOCPNetwork::ConnectionId connectionId{};

		bool connected = false;
		bool ended = false;

		std::vector<std::string> responses;
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

					state.responses.push_back(std::move(response));
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
		constexpr std::size_t PacketCount = 10;

		const auto waitTimeout = EnableDebugTimeouts
			? std::chrono::milliseconds{ DebugTimeout } : std::chrono::milliseconds{ 15'000 };

		IOCPNetwork::ConnectionId connectionId{};

		// 핸드셰이크와 connected 콜백이 끝날 때까지 기다린다.
		{
			std::unique_lock<std::mutex> lock{ state.mutex };

			const bool signaled = state.changed.wait_for(
				lock,
				waitTimeout,
				[&state]
				{
					return state.connected || state.ended;
				});

			if (!signaled || state.ended)
			{
				std::osyncstream{ std::cerr }
					<< "Connection was not ready.\n";

				return false;
			}

			connectionId = state.connectionId;

			state.responses.clear();
			state.responses.reserve(PacketCount);
		}

		// 각 응답을 구별할 수 있도록 서로 다른 본문을 준비한다.
		std::vector<std::string> messages;
		messages.reserve(PacketCount);

		for (std::size_t index = 0; index < PacketCount; ++index)
		{
			messages.push_back("Echo #" + std::to_string(index + 1) + ": Hello, IOCP!");
		}

		// 개별 응답을 기다리지 않고 모든 패킷을 순서대로 송신 접수한다.
		for (std::size_t index = 0; index < messages.size(); ++index)
		{
			const auto& message = messages[index];

			const auto payload = std::as_bytes(
				std::span<const char>{ message.data(), message.size() });

			auto packetResult = client.CreatePacket(IOCPEcho::EchoPacketId, payload);

			if (!packetResult)
			{
				std::osyncstream{ std::cerr }
					<< "Failed to create packet. sequence=" << index + 1
					<< ", error=" << static_cast<int>(packetResult.error)
					<< '\n';

				return false;
			}

			const auto sendResult = client.Send(connectionId, std::move(packetResult.packet));

			if (sendResult != IOCPNetwork::SendResult::Queued)
			{
				std::osyncstream{ std::cerr }
					<< "Send rejected. sequence=" << index + 1
					<< ", result=" << static_cast<int>(sendResult)
					<< '\n';

				return false;
			}

			std::osyncstream{ std::cout }
				<< "[Send queued] " << message << '\n';
		}

		std::vector<std::string> responses;

		// 응답 수가 목표에 도달하거나 연결이 종료될 때까지 기다린다.
		{
			std::unique_lock<std::mutex> lock{ state.mutex };

			const bool signaled = state.changed.wait_for(
				lock,
				waitTimeout,
				[&state, &messages]
				{
					return state.responses.size() >= messages.size() || state.ended;
				});

			if (!signaled || state.responses.size() != messages.size())
			{
				std::osyncstream{ std::cerr }
					<< "Unexpected echo response count. expected="
					<< messages.size()
					<< ", actual=" << state.responses.size()
					<< ", connectionEnded=" << state.ended
					<< '\n';

				return false;
			}

			// 공유 상태를 잠근 상태에서 복사하고, 비교는 락 밖에서 수행한다.
			responses = state.responses;
		}

		for (std::size_t index = 0; index < messages.size(); ++index)
		{
			if (responses[index] != messages[index])
			{
				std::osyncstream{ std::cerr }
					<< "Echo content or order mismatch. sequence="
					<< index + 1
					<< "\n  expected: " << messages[index]
					<< "\n  actual:   " << responses[index]
					<< '\n';

				return false;
			}

			std::osyncstream{ std::cout } << "[Echo verified] " << responses[index] << '\n';
		}

		std::osyncstream{ std::cout }
			<< "Echo verification passed: "
			<< responses.size()
			<< " packets, matching content and order.\n";

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

		if (EnableDebugTimeouts)
		{
			options.network.handshakeTimeout = DebugTimeout;
			options.network.heartbeatTimeout = DebugTimeout;
			options.network.receiveTimeout = DebugTimeout * 2;
			options.network.backpressureTimeout = DebugTimeout;
			options.network.shutdownTimeout = DebugTimeout;
		}

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

		if (succeeded)
		{
			std::osyncstream{ std::cout }
				<< "Echo verification completed.\n"
				<< "Press Enter to disconnect and stop EchoClient.\n";

			std::string input;
			std::getline(std::cin, input);
		}

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
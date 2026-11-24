#pragma once

#include "PacketTypes.h"

namespace IOCPEcho
{
	// 요청과 응답에 같은 ID 사용, 서버는 받은 내용을 그대로 반환
	inline constexpr IOCPNetwork::PacketId EchoPacketId = 1000;
}
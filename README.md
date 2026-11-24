# IOCPEcho

IOCPNetwork DLL의 공개 API를 사용해 에코 서버와 클라이언트를 구현하고, 연결·송수신·종료 흐름을 학습하는 C++20 예제 프로젝트입니다.

## 빌드 환경

- Windows x64
- Visual Studio 2026, C++ 데스크톱 개발 도구 및 Windows SDK
- MSVC v145, C++20
- IOCPNetwork의 동일 구성(Debug 또는 Release) x64 빌드 결과

Debug는 `/MDd`, Release는 `/MD`를 사용합니다. 예제의 지원 빌드 플랫폼은 x64입니다.

## 폴더 구조

```text
IOCPEcho/
├─ IOCPEcho.slnx
├─ Common/
│  └─ EchoProtocol.h
├─ EchoServer/
│  ├─ EchoServer.vcxproj
│  └─ EchoServer.cpp
└─ EchoClient/
   ├─ EchoClient.vcxproj
   └─ EchoClient.cpp
```

## 저장소 배치 및 의존성

엔진 소스와 바이너리는 이 저장소에 포함하지 않습니다. 현재 프로젝트 설정은 다음 상대 경로 배치를 기준으로 합니다.

```text
Workspace/
├─ Applications/
│  └─ IOCPEcho/
└─ Module/
   └─ IOCPNetwork/
      └─ IOCPNetwork/
         ├─ IOCPNetwork.h
         ├─ PacketTypes.h
         └─ x64/
            ├─ Debug/    # IOCPNetwork.lib, IOCPNetwork.dll
            └─ Release/  # IOCPNetwork.lib, IOCPNetwork.dll
```

다른 위치에 배치하면 두 프로젝트의 속성에서 다음 세 항목을 함께 변경해야 합니다.

- C/C++ → 일반 → 추가 포함 디렉터리
- 링커 → 일반 → 추가 라이브러리 디렉터리
- 빌드 이벤트 → 빌드 후 이벤트 → DLL 복사 명령

추가 종속성은 `IOCPNetwork.lib`이며, 소비 프로젝트에는 `IOCPNETWORK_EXPORTS`를 정의하지 않습니다.

## 빌드 및 실행

1. IOCPNetwork를 원하는 구성의 x64 DLL로 먼저 빌드합니다.
2. `IOCPEcho.slnx`를 Visual Studio에서 엽니다.
3. 엔진과 동일한 Debug 또는 Release 구성 및 x64 플랫폼을 선택합니다.
4. 솔루션을 빌드합니다. 빌드 후 이벤트가 DLL을 EXE 출력 폴더로 복사합니다.
5. EchoServer 또는 EchoClient를 시작 프로젝트로 설정하고 실행합니다.

출력 위치는 `Build/x64/Debug/` 또는 `Build/x64/Release/`이며, 두 EXE와 `IOCPNetwork.dll`이 같은 폴더에 배치됩니다.

엔진은 예제 솔루션에서 자동으로 빌드하지 않습니다. 엔진을 수정했다면 엔진 빌드 후 예제 프로젝트를 다시 빌드하여 DLL을 갱신하세요.

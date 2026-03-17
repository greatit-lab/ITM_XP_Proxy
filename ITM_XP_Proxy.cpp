// ITM_XP_Proxy.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
#define WIN32_LEAN_AND_MEAN
#pragma warning(disable: 4996) // XP 호환용 inet_addr 사용을 위한 경고 무시

#include <windows.h>
#include <winsock2.h>
#include <iostream>

// Winsock 라이브러리 자동 링크
#pragma comment(lib, "ws2_32.lib")

const char* TARGET_IP = "10.1172.111.93";

// 스레드에 넘겨줄 소켓 정보 구조체
struct ProxyParam {
    SOCKET srcSocket;
    SOCKET dstSocket;
};

// 양방향 데이터 릴레이 스레드 함수
DWORD WINAPI RelayData(LPVOID lpParam) {
    ProxyParam* param = (ProxyParam*)lpParam;
    SOCKET src = param->srcSocket;
    SOCKET dst = param->dstSocket;
    delete param; // 메모리 해제

    char buffer[8192];
    int bytesRead;

    // 데이터 수신 및 전달
    while ((bytesRead = recv(src, buffer, sizeof(buffer), 0)) > 0) {
        int totalSent = 0;
        while (totalSent < bytesRead) {
            int sent = send(dst, buffer + totalSent, bytesRead - totalSent, 0);
            if (sent <= 0) break;
            totalSent += sent;
        }
    }

    // 통신 종료 시 양쪽 소켓 모두 닫기
    shutdown(src, SD_BOTH);
    shutdown(dst, SD_BOTH);
    closesocket(src);
    closesocket(dst);
    return 0;
}

// 클라이언트 접속 처리 함수
void HandleConnection(SOCKET clientSocket, int targetPort) {
    SOCKET targetSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (targetSocket == INVALID_SOCKET) {
        closesocket(clientSocket);
        return;
    }

    sockaddr_in targetAddr;
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(targetPort);
    targetAddr.sin_addr.s_addr = inet_addr(TARGET_IP); // XP 호환용 구형 API

    // 외부망 서버로 연결 시도
    if (connect(targetSocket, (SOCKADDR*)&targetAddr, sizeof(targetAddr)) == SOCKET_ERROR) {
        closesocket(clientSocket);
        closesocket(targetSocket);
        return;
    }

    // 양방향 통신을 위한 2개의 스레드 생성 (Client <-> Proxy <-> Server)
    ProxyParam* p1 = new ProxyParam{ clientSocket, targetSocket };
    ProxyParam* p2 = new ProxyParam{ targetSocket, clientSocket };

    CreateThread(NULL, 0, RelayData, p1, 0, NULL);
    CreateThread(NULL, 0, RelayData, p2, 0, NULL);
}

// 리스너 스레드 함수 (포트 대기)
DWORD WINAPI StartListener(LPVOID lpParam) {
    int* ports = (int*)lpParam;
    int localPort = ports[0];
    int targetPort = ports[1];
    delete[] ports;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(localPort);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    std::cout << "  [Proxy] Listening on port " << localPort << " -> Forwarding to " << targetPort << std::endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket != INVALID_SOCKET) {
            HandleConnection(clientSocket, targetPort);
        }
    }
    return 0;
}

int main() {
    // 콘솔 창 제목 설정
    SetConsoleTitleA("ITM Agent Proxy Server for Windows XP");

    // Winsock 초기화
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "WSAStartup failed." << std::endl;
        return 1;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "  [ITM Proxy Server] Started for Windows XP (C++)" << std::endl;
    std::cout << "  Target Server : " << TARGET_IP << std::endl;
    std::cout << "=================================================\n" << std::endl;

    // 1. DB 포트 포워딩 스레드 (15432 -> 5432)
    int* dbPorts = new int[2]{ 15432, 5432 };
    CreateThread(NULL, 0, StartListener, dbPorts, 0, NULL);

    // 2. API 포트 포워딩 스레드 (18082 -> 8082)
    int* apiPorts = new int[2]{ 18082, 8082 };
    CreateThread(NULL, 0, StartListener, apiPorts, 0, NULL);

    // 메인 스레드는 종료되지 않고 무한 대기
    Sleep(INFINITE);

    WSACleanup();
    return 0;
}

// Regression checks for the bundled HTTP library and the WebSocket API used by PBC.
#define httplib pbc_httplib
#include <httplib.h>

#include <iostream>
#include <stdexcept>
#include <thread>

static void Check(bool value, char const* message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct RunningServer
{
    httplib::Server& server;
    std::thread thread;

    explicit RunningServer(httplib::Server& value)
        : server(value), thread([&value] { value.listen_after_bind(); }) { }

    ~RunningServer()
    {
        server.stop();
        thread.join();
    }
};

static void TestHeadersAndChunks()
{
    httplib::Server server;
    server.set_read_timeout(2);
    server.Get("/header", [](auto const& req, auto& res)
    {
        res.set_content(req.get_header_value("X-Test"), "text/plain");
    });
    server.Post("/body", [](auto const&, auto& res) { res.set_content("ok", "text/plain"); });
    int port = server.bind_to_any_port("127.0.0.1");
    Check(port > 0, "HTTP bind failed");
    RunningServer running(server);
    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(2);
    auto response = client.Get("/header", httplib::Headers{{"X-Test", "a%0D%0AInjected: b"}});
    Check(response && response->status == 200 && response->body == "a%0D%0AInjected: b",
          "HTTP header was percent-decoded into control characters");

#ifndef _WIN32
    // Send raw chunk framing, without a client library normalizing invalid input.
    struct Socket
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ~Socket() { if (fd >= 0) ::close(fd); }
    } socket;
    Check(socket.fd >= 0, "socket failed");
    timeval timeout{2, 0};
    Check(setsockopt(socket.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
          "receive timeout failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(::connect(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "raw HTTP connect failed");
    std::string request = "POST /body HTTP/1.1\r\nHost: localhost\r\n"
                          "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n-2\r\nx\r\n0\r\n\r\n";
    std::size_t sent = 0;
    while (sent < request.size())
    {
        auto count = ::send(socket.fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        Check(count > 0, "raw HTTP send failed");
        sent += static_cast<std::size_t>(count);
    }
    std::string received;
    char buffer[512];
    while (received.find("\r\n") == std::string::npos && received.size() < 4096)
    {
        auto count = ::recv(socket.fd, buffer, sizeof(buffer), 0);
        Check(count > 0, "negative chunk was not rejected promptly");
        received.append(buffer, static_cast<std::size_t>(count));
    }
    Check(received.rfind("HTTP/1.1 400", 0) == 0, "negative chunk size accepted");
    response = client.Get("/header", httplib::Headers{{"X-Test", "still-alive"}});
    Check(response && response->body == "still-alive", "server failed after invalid chunk");
#endif
}

static void TestWebSocket(httplib::Server& server, std::string const& scheme,
                          std::string const& caFile = "")
{
    server.WebSocket("/ws", [](auto const&, httplib::ws::WebSocket& ws)
    {
        ws.set_read_timeout(2);
        ws.send("connected");
        std::string message;
        if (ws.read(message))
            ws.send(message);
        ws.close();
    }, [](std::vector<std::string> const& protocols)
    {
        for (auto const& protocol : protocols)
            if (protocol == "access_token")
                return protocol;
        return std::string{};
    });
    int port = server.bind_to_any_port("127.0.0.1");
    Check(port > 0, "WebSocket bind failed");
    RunningServer running(server);
    httplib::ws::WebSocketClient client(scheme + "://127.0.0.1:" + std::to_string(port) + "/ws",
                                       {{"Sec-WebSocket-Protocol", "access_token, fixture-token"}});
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    client.set_write_timeout(2);
    if (!caFile.empty())
        client.set_ca_cert_path(caFile);
    Check(static_cast<bool>(client.connect()), "WebSocket handshake failed");
    Check(client.subprotocol() == "access_token", "PBC subprotocol negotiation failed");
    std::string message;
    Check(client.read(message) && message == "connected", "WebSocket event missing");
    Check(client.send("round-trip"), "WebSocket send failed");
    Check(client.read(message) && message == "round-trip", "WebSocket round-trip failed");
    client.close();
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "http")
            TestHeadersAndChunks();
        else
        {
            Check(argc == 3, "expected certificate and key paths");
            httplib::Server plain;
            TestWebSocket(plain, "ws");
            httplib::SSLServer tls(argv[1], argv[2]);
            Check(tls.is_valid(), "TLS server initialization failed");
            TestWebSocket(tls, "wss", argv[1]);
        }
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

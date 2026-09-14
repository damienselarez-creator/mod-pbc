#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif
#include "pbc_http.h"
#include "Config.h"
#include "pbc_log.h"
#include <httplib.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstdlib>
void Check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
struct ServerThread {
 httplib::Server& server;
 std::thread thread;
 ServerThread(httplib::Server& s):server(s),thread([&s]{s.listen_after_bind();}) {}
 ~ServerThread() { server.stop(); thread.join(); }
};
int TestTls(int argc,char** argv) {
 Check(argc==5,"certificate arguments");
 httplib::SSLServer tls(argv[1],argv[2]);
 tls.Post("/ok",[](auto const&,auto& res){res.set_content("trusted","text/plain");});
 int port=tls.bind_to_any_port("127.0.0.1"); Check(port>0,"TLS bind");
 ServerThread tlsThread(tls);
 PBC_HttpClient client; client.SetTimeoutSeconds(2);
 std::string base="https://127.0.0.1:"+std::to_string(port);
 Check(client.Post(base+"/ok","{}").empty(),"untrusted certificate accepted");
 testCaFile=argv[1];
 httplib::SSLClient diagnostic("127.0.0.1",port);
 diagnostic.set_ca_cert_path(testCaFile);
 bool intercepted = false;
 diagnostic.set_session_verifier([&](httplib::tls::session_t session) {
  auto cert=httplib::tls::get_peer_cert(session);
  auto issuer=httplib::tls::get_cert_issuer_name(cert);
  intercepted = issuer.find("Avast") != std::string::npos;
  std::cerr << "Test certificate issuer: " << issuer << std::endl;
  httplib::tls::free_cert(cert);
  return httplib::SSLVerifierResponse::NoDecisionMade;
 });
 auto diagnosticResult=diagnostic.Post("/ok","{}","application/json");
 if (!diagnosticResult && intercepted) {
  std::cerr << "TLS fixture intercepted by Avast; trusted-certificate and hostname tests inconclusive.\n";
  return std::getenv("PBC_REQUIRE_TLS_FIXTURE") ? 1 : 77;
 }
 Check(client.Post(base+"/ok","{}") == "trusted","trusted certificate rejected");
 httplib::SSLServer wrong(argv[3],argv[4]);
 wrong.Post("/ok",[](auto const&,auto& res){res.set_content("wrong","text/plain");});
 int wrongPort=wrong.bind_to_any_port("127.0.0.1"); Check(wrongPort>0,"wrong-host bind");
 ServerThread wrongThread(wrong); testCaFile=argv[3];
 Check(client.Post("https://127.0.0.1:"+std::to_string(wrongPort)+"/ok","{}").empty(),"hostname mismatch accepted");
 std::cout << "TLS trust and hostname verified.\n";
 return 0;
}

int TestHttp() {
 PBC_HttpClient client; client.SetTimeoutSeconds(2);
 httplib::Server plain;
 plain.Post("/ok",[](auto const&,auto& res){res.set_content("ok","text/plain");});
 plain.Post("/error",[](auto const&,auto& res){res.status=500;res.set_content("failure","text/plain");});
 plain.Post("/redirect",[](auto const&,auto& res){res.set_redirect("/ok");});
 plain.Post("/large",[](auto const&,auto& res){res.set_content(std::string(3*1024*1024,'x'),"text/plain");});
 plain.Post("/slow",[](auto const&,auto& res){std::this_thread::sleep_for(std::chrono::seconds(2));res.set_content("late","text/plain");});
 int httpPort=plain.bind_to_any_port("127.0.0.1"); Check(httpPort>0,"HTTP bind");
 ServerThread plainThread(plain); std::string base="http://127.0.0.1:"+std::to_string(httpPort);
 Check(client.Post(base+"/ok","{}")=="ok","HTTP failed");
 for(auto path:{"/error","/redirect","/large"}) Check(client.Post(base+path,"{}").empty(),path);
 client.SetTimeoutSeconds(1);
 auto start=std::chrono::steady_clock::now();
 Check(client.Post(base+"/slow","{}").empty(),"late response accepted");
 Check(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(1800),"timeout not bounded");
 Check(client.Post("http://user@localhost/ok","{}").empty(),"userinfo accepted");
 Check(client.Post("http://localhost:70000/ok","{}").empty(),"invalid port accepted");
 std::cout<<"HTTP: status/redirect/oversize/timeout rejected.\n";
 return 0;
}
int main(int argc,char** argv) try {
 if(argc==2 && std::string(argv[1])=="http") return TestHttp();
 if(argc==2 && std::string(argv[1])=="public") {
  PBC_HttpClient client; client.SetTimeoutSeconds(10);
  // Optional network smoke test: no key, no conversation, no paid completion.
  Check(client.Post("https://api.openai.com/v1/chat/completions","{}").empty(),"unexpected unauthenticated completion");
  Check(testLastError.find("HTTP {} from {}:{}{} 401 ")==0,"TLS/API endpoint did not reach authenticated HTTP rejection");
  std::cout << "Public TLS verified; unauthenticated request correctly rejected with HTTP 401.\n";
  return 0;
 }
 return TestTls(argc,argv);
} catch(std::exception const& error) { std::cerr<<error.what()<<std::endl; return 1; }

#include "pbc_llm.h"
#include "pbc_config.h"
#include "pbc_http.h"
#include <iostream>
#include <stdexcept>
std::string reply, request, url;
int calls=0, timeout=0;
PBC_HttpClient::PBC_HttpClient():m_timeoutSec(120) {}
void PBC_HttpClient::SetTimeoutSeconds(int value) { timeout=value; }
std::string PBC_HttpClient::Post(const std::string& u,const std::string& body,const std::vector<std::pair<std::string,std::string>>&) {
 ++calls; request=body; url=u; return reply;
}
void Check(bool ok) { if (!ok) throw std::runtime_error("LLM contract failed"); }
int main() {
 PBC_APIConfig cfg{"openai","https://example.invalid/v1","","test",120,pbc_json{{"stream",true}}};
 auto run=[&](std::string body,bool accepted) {
  reply=body; int before=calls;
  auto result=PBC_CallLLMWithConfig(cfg,"system","user",true);
  Check(calls==before+1 && result.success==accepted);
  Check(pbc_json::parse(request).at("stream")==false);
  return result;
 };
 Check(run(R"({"choices":[{"finish_reason":"stop","message":{"content":" Bonjour "}}]})",true).text=="Bonjour");
 for (auto reason:{"length","content_filter","tool_calls",""})
  run(pbc_json{{"choices",pbc_json::array({{{"finish_reason",reason},{"message",{{"content","partial"}}}}})}}.dump(),false);
 run(R"({"choices":[{"message":{"content":"missing metadata"}}]})",false);
 run(R"({"choices":[{"finish_reason":"stop","message":{"content":"text","refusal":"refused"}}]})",false);
 run(R"({"choices":[{"finish_reason":"stop","message":{"content":"text","tool_calls":[{}]}}]})",false);
 run(R"({"choices":[{"finish_reason":"stop","message":{"content":"  "}}]})",false);
 run(R"({"choices":[{"finish_reason":"stop","message":{"content":null}}]})",false);
 run(R"({"error":{"message":"rate limited"}})",false);
 run("{broken",false); run("",false);
 cfg.apiType="anthropic";
 Check(run(R"({"stop_reason":"end_turn","content":[{"type":"thinking","thinking":"private"},{"type":"text","text":"A"},{"type":"text","text":"B"}]})",true).text=="AB");
 Check(url=="https://example.invalid/v1/messages");
 for(auto reason:{"max_tokens","refusal","tool_use","pause_turn","model_context_window_exceeded"})
  run(pbc_json{{"stop_reason",reason},{"content",pbc_json::array({{{"type","text"},{"text","partial"}}})}}.dump(),false);
 cfg.apiType="ollama";
 run(R"({"done":true,"done_reason":"stop","message":{"content":"hello"}})",true);
 run(R"({"done":false,"done_reason":"stop","message":{"content":"partial"}})",false);
 run(R"({"done":true,"done_reason":"length","message":{"content":"partial"}})",false);
 run(R"({"done":true,"done_reason":"stop","message":{"content":"text","tool_calls":[{}]}})",false);
 Check(!PBC_CallLLM("sys","user").success);
 testConnection=std::make_shared<const PBC_APIConfig>(cfg);
 reply=R"({"done":true,"done_reason":"stop","message":{"content":"hello"}})";
 Check(PBC_CallLLM("sys","user",false,3).success && timeout==3);
 std::cout<<"LLM: complete text only, refusals/truncation/errors rejected, one attempt, stream disabled, timeout capped.\n";
}

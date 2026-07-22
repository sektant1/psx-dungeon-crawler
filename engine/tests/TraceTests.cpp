#include <eng/Trace.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace eng;
static void require(bool c, const char* m){ if(!c){ std::cerr<<"TraceTests: "<<m<<'\n'; std::exit(1);} }

int main(){
    const char* path = "/tmp/eng_trace_test.json";
    {
        Trace t;
        t.beginSession(path);
        { Trace::Scope s(t, "outer"); { Trace::Scope s2(t, "inner"); } }
        t.endSession();
    }
    std::ifstream in(path);
    require(in.good(), "trace file written");
    std::stringstream ss; ss << in.rdbuf();
    const std::string json = ss.str();
    require(json.find("\"traceEvents\"") != std::string::npos, "has traceEvents key");
    require(json.find("\"outer\"") != std::string::npos, "recorded outer scope");
    require(json.find("\"inner\"") != std::string::npos, "recorded inner scope");
    require(json.find("\"ph\":\"X\"") != std::string::npos, "complete-event phase");
    std::cout << "TraceTests OK\n";
    return 0;
}

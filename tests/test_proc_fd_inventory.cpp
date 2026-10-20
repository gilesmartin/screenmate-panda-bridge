#include "../native/proc_fd_inventory.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

static void link_fd(const std::string& dir, const char* number, const char* target) {
    assert(symlink(target, (dir+"/"+number).c_str())==0);
}
int main() {
    char templ[]="/tmp/hermes-fd-XXXXXX";
    char* raw=mkdtemp(templ); assert(raw);
    std::string dir(raw);
    assert(!no_can_session_fds(dir+"/missing"));
    link_fd(dir,"0","/dev/null");
    link_fd(dir,"1","/dev/binderfs/binder");
    link_fd(dir,"2","socket:[1234]");
    link_fd(dir,"3","/dev/ttyACM4");
    link_fd(dir,"4","/sys/kernel/tracing/trace_marker");
    assert(no_can_session_fds(dir));
    link_fd(dir,"5","/dev/ashmem/can_stream (deleted)");
    assert(!no_can_session_fds(dir));
    assert(unlink((dir+"/5").c_str())==0);
    link_fd(dir,"5","/memfd:can_stream (deleted)");
    assert(!no_can_session_fds(dir));
    assert(unlink((dir+"/5").c_str())==0);
    link_fd(dir,"5","/unexpected/session");
    assert(!no_can_session_fds(dir));
    for(int i=0;i<=5;++i) unlink((dir+"/"+std::to_string(i)).c_str());
    assert(rmdir(dir.c_str())==0);
}

#pragma once
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <cctype>
#include <string>

// Diagnostic for THIS firmware only: each live CAN stream owns an ashmem fd
// (verified against the installed openCanStream implementation). Unknown fd
// types or an incomplete /proc snapshot are NOT proof of zero sessions.
inline bool no_can_session_fds(const std::string& dir) {
    DIR* d=opendir(dir.c_str());
    if (!d) return false;
    bool safe=true;
    unsigned count=0;
    for (dirent* e; (e=readdir(d))!=nullptr;) {
        const std::string entry=e->d_name;
        if (entry=="." || entry=="..") continue;
        if (entry.empty()) { safe=false; break; }
        bool numeric=true;
        for (char c:entry) if (!std::isdigit(static_cast<unsigned char>(c))) numeric=false;
        if (!numeric || ++count>256) { safe=false; break; }
        char target[PATH_MAX];
        const std::string path=dir+"/"+entry;
        const ssize_t n=readlink(path.c_str(),target,sizeof(target)-1);
        if (n<=0 || n>=ssize_t(sizeof(target)-1)) { safe=false; break; }
        target[n]='\0';
        const std::string value(target);
        if (value.find("ashmem")!=std::string::npos ||
            value.find("memfd")!=std::string::npos) { safe=false; break; }
        const bool known=value=="/dev/null" || value=="/dev/binderfs/binder" ||
            value=="/sys/kernel/tracing/trace_marker" ||
            value.compare(0,11,"/dev/ttyACM")==0 ||
            value.compare(0,8,"socket:[")==0 ||
            value.compare(0,11,"anon_inode:")==0;
        if (!known) { safe=false; break; }
    }
    if (closedir(d)!=0) safe=false;
    return safe && count>0;
}

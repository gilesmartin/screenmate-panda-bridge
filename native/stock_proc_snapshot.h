#pragma once
#include "stock_guard.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// This firmware-specific collector reads only procfs metadata. It does not open
// the stock ring, call Binder, or query the vendor dumpsys endpoint.
namespace stock_proc {
inline constexpr const char* kMcuExe="/odm/bin/hw/vendor.teslogic.hardware.mcu.service";
inline constexpr const char* kVhalExe="/odm/bin/hw/android.hardware.automotive.vehicle.service-teslogic";
inline bool link(const std::string& path,std::string& out) {
    char buf[4096]; const auto n=readlink(path.c_str(),buf,sizeof(buf)-1);
    if(n<=0 || n>=static_cast<ssize_t>(sizeof(buf)-1)) return false;
    out.assign(buf,static_cast<size_t>(n)); return true;
}
inline bool fingerprint(const std::string& path,std::string& out) {
    struct stat st{}; if(stat(path.c_str(),&st)!=0) return false;
    out=std::to_string(static_cast<unsigned long long>(st.st_dev))+":"+
        std::to_string(static_cast<unsigned long long>(st.st_ino))+":"+
        std::to_string(static_cast<unsigned long long>(st.st_size));
    return true;
}
inline bool numeric(const char* value) {
    if(!*value) return false;
    for(const char* p=value;*p;++p) if(*p<'0'||*p>'9')return false;
    return true;
}
inline bool birth(const std::string& path,std::string& out) {
    std::ifstream in(path); std::string line;
    if(!std::getline(in,line)) return false;
    auto pos=line.rfind(") "); if(pos==std::string::npos) return false;
    std::istringstream rest(line.substr(pos+2)); std::string word;
    // field 3 is the first item after comm, and starttime is field 22.
    for(int field=3;field<=22;++field) if(!(rest>>word))return false;
    out=word; return !out.empty();
}
inline bool exact_pid(const std::string& root,const char* expected,int& pid,std::string& identity) {
    DIR* dir=opendir(root.c_str()); if(!dir)return false;
    int count=0, found=-1; std::string found_birth;
    while(true) {
        errno=0; dirent* ent=readdir(dir);
        if(!ent)break;
        if(!numeric(ent->d_name))continue;
        const std::string p=root+"/"+ent->d_name;
        std::string target;
        if(!link(p+"/exe",target)||target!=expected)continue;
        std::string started;
        if(!birth(p+"/stat",started)){++count;continue;}
        ++count; found=std::atoi(ent->d_name); found_birth=started;
    }
    const bool complete=(errno==0);
    closedir(dir);
    if(!complete || count!=1 || found<=0 || found_birth.empty())return false;
    pid=found; identity=std::string(expected)+"@"+found_birth; return true;
}
inline bool mcu_fds(const std::string& root,int pid,std::vector<stock_guard::Fd>& out) {
    const std::string dirpath=root+"/"+std::to_string(pid)+"/fd";
    DIR* dir=opendir(dirpath.c_str()); if(!dir)return false;
    bool good=true; errno=0;
    while(true) {
        errno=0; dirent* ent=readdir(dir);
        if(!ent)break;
        if(!numeric(ent->d_name))continue;
        const std::string path=dirpath+"/"+ent->d_name;
        std::string target;
        if(!link(path,target)){good=false;break;}
        if(target.find("ashmem")==std::string::npos &&
           target.find("memfd")==std::string::npos)continue;
        std::string stat;
        if(!fingerprint(path,stat)){good=false;break;}
        out.push_back({std::atoi(ent->d_name),target,stat});
    }
    if(errno!=0)good=false;
    closedir(dir);
    return good;
}
inline bool vhal_maps(const std::string& root,int pid,std::vector<stock_guard::Mapping>& out) {
    const std::string prefix=root+"/"+std::to_string(pid);
    std::ifstream maps(prefix+"/maps"); if(!maps)return false;
    std::string line;
    while(std::getline(maps,line)) {
        if(line.find("teslogic_can_stream")==std::string::npos)continue;
        std::istringstream in(line); std::string range,perms,offset,device,inode;
        if(!(in>>range>>perms>>offset>>device>>inode))return false;
        std::string target; std::getline(in,target);
        const auto pos=target.find_first_not_of(' ');
        if(pos==std::string::npos)return false;
        target.erase(0,pos);
        const std::string path=prefix+"/map_files/"+range;
        std::string linked,stat;
        if(!link(path,linked)||linked!=target||!fingerprint(path,stat))return false;
        out.push_back({target,perms,stat});
    }
    return !maps.bad() && !out.empty();
}
inline bool snapshot(stock_guard::Snapshot& out,const std::string& root="/proc") {
    stock_guard::Snapshot s;
    std::string mcu_identity, vhal_identity;
    if(!exact_pid(root,kMcuExe,s.mcu_pid,mcu_identity)||
       !exact_pid(root,kVhalExe,s.vhal_pid,vhal_identity))return false;
    s.mcu_binary=mcu_identity;
    s.vhal_name=stock_guard::kVhalName;
    s.vhal_birth=vhal_identity;
    if(!mcu_fds(root,s.mcu_pid,s.stock_fds) ||
       !vhal_maps(root,s.vhal_pid,s.vhal_maps))return false;
    out=std::move(s); return true;
}
}

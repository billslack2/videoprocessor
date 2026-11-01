#pragma once
#include <stdexcept>
#include <string>

// Storage location changes no libplacebo cache keys or compatibility checks.
namespace ShaderCacheStorage {
inline std::string Resolve(const std::string& directory,const std::string& executable,
    const std::string& legacyRelative,bool beta,bool clearRequest=false) {
    if(directory.empty()) {
        const auto separator=executable.find_last_of("\\/");
        return separator==std::string::npos?legacyRelative:executable.substr(0,separator+1)+legacyRelative;
    }
    const auto slash=[](char c){return c=='\\' || c=='/';};
    const bool drive=directory.size()>=3 &&
        ((directory[0]>='A' && directory[0]<='Z') || (directory[0]>='a' && directory[0]<='z')) &&
        directory[1]==':' && slash(directory[2]);
    bool unc=false;
    if(directory.size()>4 && slash(directory[0]) && slash(directory[1])) {
        const auto serverEnd=directory.find_first_of("\\/",2);
        unc=serverEnd!=std::string::npos && serverEnd>2 && serverEnd+1<directory.size() &&
            !slash(directory[serverEnd+1]) && directory.substr(2,serverEnd-2)!="?" &&
            directory.substr(2,serverEnd-2)!=".";
    }
    if(!drive && !unc)throw std::invalid_argument("VP_SHADER_CACHE_DIRECTORY must be an absolute directory");
    return directory+(slash(directory.back())?"":"\\")+"VideoProcessorShaderCache-"+
        (beta?"beta":"standard")+(clearRequest?".clear":".bin");
}
inline std::string TemporaryPath(const std::string& path,unsigned long processId,unsigned long long nonce) {
    return path+".tmp."+std::to_string(processId)+"."+std::to_string(nonce);
}
}

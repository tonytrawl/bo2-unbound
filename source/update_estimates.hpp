#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>

namespace unbound {
struct UpdateTimingHistory {
    double download=0,usb=0,nand=0,sd=0;
    static std::uint64_t number(const std::string& value){
        if(value.empty()||value.size()>16)return 0;
        std::uint64_t n=0;for(char c:value){if(c<'0'||c>'9')return 0;n=n*10+unsigned(c-'0');}return n;
    }
    static void sample(double& rate,std::uint64_t bytes,std::uint64_t ms){
        if(bytes<1024*1024||bytes>16ULL*1024*1024*1024||ms<250||ms>24*60*60*1000ULL)return;
        const double value=double(bytes)*1000/double(ms);
        if(value<16*1024||value>512*1024*1024)return;
        rate=rate?rate*.65+value*.35:value;
    }
    void read(const std::string& log){
        std::istringstream lines(log);std::string line;
        while(std::getline(lines,line)){
            std::istringstream fields(line);std::string revision,kind,field;
            std::getline(fields,revision,'|');std::getline(fields,kind,'|');
            std::map<std::string,std::string> values;
            while(std::getline(fields,field,'|')){if(!field.empty()&&field.back()=='\r')field.pop_back();auto eq=field.find('=');if(eq!=std::string::npos)values[field.substr(0,eq)]=field.substr(eq+1);}
            if(values["ok"]!="1")continue;
            if(kind=="download")sample(download,number(values["network_bytes"]),number(values["total_ms"]));
            else if(kind=="install"){
                const auto bytes=number(values["compressed_bytes"]),ms=number(values["total_ms"]);
                if(values["device"]=="USB")sample(usb,bytes,ms);
                else if(values["device"]=="NAND")sample(nand,bytes,ms);
                else if(values["device"]=="SD")sample(sd,bytes,ms);
            }
        }
    }
    static std::uint64_t seconds(std::uint64_t bytes,double rate,double initial){
        return bytes?std::max<std::uint64_t>(1,std::uint64_t(std::ceil(double(bytes)/(rate?rate:initial)))):0;
    }
};
inline std::string updateDuration(std::uint64_t seconds){
    if(!seconds)return "0 sec";
    if(seconds<60)return "<1 min";
    const auto minutes=(seconds+59)/60;
    return minutes<60?std::to_string(minutes)+" min":std::to_string(minutes/60)+" hr "+std::to_string(minutes%60)+" min";
}
}

#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cctype>
#include <cmath>
namespace unbound::network_json {
struct Value {
 char type=0; double number=0; bool boolean=false;
 std::string string; std::map<std::string,Value> object; std::vector<Value> array;
 const Value& get(const char* k)const{static const Value empty;auto i=object.find(k);return i==object.end()?empty:i->second;}
 bool count(unsigned& out)const{if(type!='n'||number<0||number>10000000||std::floor(number)!=number)return false;out=static_cast<unsigned>(number);return true;}
};
class Parser {
 const std::string& s;size_t p=0;unsigned nodes=0;
 void ws(){while(p<s.size()&&std::isspace(static_cast<unsigned char>(s[p])))++p;}
 bool str(std::string& out){if(p>=s.size()||s[p++]!='"')return false;while(p<s.size()){char c=s[p++];if(c=='"')return true;if(static_cast<unsigned char>(c)<32)return false;if(c=='\\'){if(p==s.size())return false;c=s[p++];if(c=='u'){for(int i=0;i<4;++i){if(p==s.size()||!std::isxdigit(static_cast<unsigned char>(s[p++])))return false;}out+='?';continue;}if(std::string("\"\\/bfnrt").find(c)==std::string::npos)return false;}out+=c;}return false;}
 bool value(Value& v,int depth){ws();if(depth>12||++nodes>4096||p>=s.size())return false;char c=s[p];
  if(c=='{'||c=='['){++p;v.type=c;ws();char end=c=='{'?'}':']';if(p<s.size()&&s[p]==end){++p;return true;}for(;;){Value item;std::string key;if(c=='{'){if(!str(key))return false;ws();if(p==s.size()||s[p++]!=':')return false;}if(!value(item,depth+1))return false;if(c=='{'){if(!v.object.emplace(key,std::move(item)).second)return false;}else v.array.push_back(std::move(item));ws();if(p==s.size())return false;char next=s[p++];if(next==end)return true;if(next!=',')return false;ws();}}
  if(c=='"'){v.type='s';return str(v.string);}
  for(const auto& literal:{std::string("true"),std::string("false"),std::string("null")})if(s.compare(p,literal.size(),literal)==0){p+=literal.size();v.type=literal=="null"?'0':'b';v.boolean=literal=="true";return true;}
  size_t begin=p;if(c=='-')++p;if(p==s.size()||!std::isdigit(static_cast<unsigned char>(s[p])))return false;if(s[p]=='0')++p;else while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;
  if(p<s.size()&&s[p]=='.'){++p;size_t q=p;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;if(q==p)return false;}
  if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;size_t q=p;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;if(q==p)return false;}
  v.number=std::strtod(s.substr(begin,p-begin).c_str(),nullptr);v.type='n';return std::isfinite(v.number);
 }
public: explicit Parser(const std::string& in):s(in){} bool parse(Value& v){if(s.size()>65536)return false;bool ok=value(v,0);ws();return ok&&p==s.size();}
};
}

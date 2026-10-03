#include <forge/engine.hpp>
#include <forge/localization_data.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif
namespace forge {
namespace {
std::string tag(std::string value) {
    for(auto& c:value){if(c=='_')c='-';else if(c>='A' && c<='Z')c=char(c-'A'+'a');}
    if(value.empty() || value.front()=='-' || value.back()=='-')throw std::runtime_error("Invalid language tag: "+value);
    bool dash=false;for(char c:value){bool valid=(c>='a' && c<='z') || (c>='0' && c<='9') || c=='-';if(!valid || (dash && c=='-'))throw std::runtime_error("Invalid language tag: "+value);dash=c=='-';}
    return value;
}
std::vector<std::string> parents(std::string code){std::vector<std::string> result;for(;;){result.push_back(code);auto p=code.rfind('-');if(p==std::string::npos)break;code.resize(p);}return result;}
std::string installed(const std::map<std::string,Localization::Catalog>& catalogs,const std::string& input){for(auto& code:parents(tag(input)))if(catalogs.count(code))return code;return {};}
const std::set<std::string> categories={"zero","one","two","few","many","other"};
struct Relation { char operand; unsigned mod=0; bool negate=false; std::vector<std::pair<double,double>> ranges; };
using Rule=std::vector<std::vector<Relation>>;
using Rules=std::map<std::string,std::map<std::string,Rule>>;
std::vector<std::string> tokens(std::string input){auto replace=[&](const std::string& from,const std::string& to){size_t p=0;while((p=input.find(from,p))!=std::string::npos){input.replace(p,from.size(),to);p+=to.size();}};replace(".."," .. ");replace(","," , ");std::istringstream stream(input);std::vector<std::string> out;for(std::string t;stream>>t;)out.push_back(t);return out;}
Rule parseRule(const std::string& expression){
    if(expression.empty())return {};
    auto words=tokens(expression);size_t i=0;Rule groups(1);
    while(i<words.size()){
        Relation relation{};auto variable=words.at(i++);if(variable.size()!=1 || std::string("nivwftce").find(variable[0])==std::string::npos)throw std::runtime_error("Invalid embedded CLDR operand");relation.operand=variable[0];
        if(words.at(i)=="%"){++i;relation.mod=unsigned(std::stoul(words.at(i++)));if(!relation.mod)throw std::runtime_error("Invalid CLDR modulus");}
        auto op=words.at(i++);if(op!="=" && op!="!=")throw std::runtime_error("Invalid embedded CLDR relation");relation.negate=op=="!=";
        do{double low=std::stod(words.at(i++)),high=low;if(i<words.size() && words[i]==".."){++i;high=std::stod(words.at(i++));}relation.ranges.push_back({low,high});if(i>=words.size() || words[i]!=",")break;++i;}while(true);
        groups.back().push_back(relation);
        if(i<words.size()){auto join=words.at(i++);if(join=="or")groups.emplace_back();else if(join!="and")throw std::runtime_error("Invalid CLDR conjunction");}
    }return groups;
}
const Rules& pluralRules(){static Rules result=[](){Rules rules;auto data=Json::parse(localization_data::plurals);for(auto it=data.begin();it!=data.end();++it)for(auto form=it.value().begin();form!=it.value().end();++form)rules[it.key()][form.key()]=parseRule(form.value());return rules;}();return result;}
std::string pluralLanguage(std::string code){if(code=="other")return code;for(auto& candidate:parents(tag(code)))if(pluralRules().count(candidate))return candidate;return {};}
struct Operand { double value=0; std::string integer; };
std::map<char,Operand> operands(const Json& count){
    if(!count.is_number() || !std::isfinite(count.get<double>()))throw std::runtime_error("Localization count must be a finite number");
    auto number=count.dump();if(number.front()=='-')number.erase(0,1);
    auto exponent=number.find_first_of("eE");int shift=0;if(exponent!=std::string::npos){shift=std::stoi(number.substr(exponent+1));number.resize(exponent);}
    auto point=number.find('.');int decimal=point==std::string::npos?int(number.size()):int(point);if(point!=std::string::npos)number.erase(point,1);decimal+=shift;
    if(decimal<0){number.insert(0,size_t(-decimal),'0');decimal=0;}if(decimal>int(number.size()))number.append(size_t(decimal-int(number.size())),'0');
    auto whole=number.substr(0,size_t(decimal));if(whole.empty())whole="0";auto fraction=number.substr(size_t(decimal));auto trimmed=fraction;while(!trimmed.empty() && trimmed.back()=='0')trimmed.pop_back();
    auto integer=[](std::string digits){if(digits.empty())digits="0";return Operand{std::strtod(digits.c_str(),nullptr),digits};};
    double n=std::abs(count.get<double>());
    return {{'n',{n,trimmed.empty()?whole:""}},{'i',integer(whole)},{'v',integer(std::to_string(fraction.size()))},{'w',integer(std::to_string(trimmed.size()))},{'f',integer(fraction)},{'t',integer(trimmed)},{'c',integer("0")},{'e',integer("0")}};
}
bool matches(const Relation& relation,const std::map<char,Operand>& values){
    const auto& operand=values.at(relation.operand);double value=operand.value;
    if(relation.mod){if(!operand.integer.empty()){unsigned remainder=0;for(char c:operand.integer)remainder=(remainder*10+unsigned(c-'0'))%relation.mod;value=remainder;}else value=std::fmod(value,relation.mod);}
    bool hit=false;for(auto [low,high]:relation.ranges)if(value>=low && value<=high && (low==high || std::floor(value)==value))hit=true;return relation.negate?!hit:hit;
}
std::string plural(const std::string& language,const Json& count){
    auto values=operands(count);if(language=="other")return "other";
    for(auto& category:{"zero","one","two","few","many"}){auto& rules=pluralRules().at(language);auto found=rules.find(category);if(found==rules.end())continue;
        for(auto& group:found->second){bool match=true;for(auto& relation:group)if(!matches(relation,values)){match=false;break;}if(match)return category;}}
    return "other";
}
std::string format(const std::string& source,const Json* params,std::set<std::string>* names=nullptr){
    std::string out;
    for(size_t i=0;i<source.size();){char c=source[i++];if(c!='{' && c!='}'){out+=c;continue;}if(i<source.size() && source[i]==c){out+=c;++i;continue;}
        if(c=='}')throw std::runtime_error("Unmatched } in localization text");auto end=source.find('}',i);if(end==std::string::npos)throw std::runtime_error("Unmatched { in localization text");auto name=source.substr(i,end-i);
        if(name.empty() || !((name[0]>='a' && name[0]<='z') || (name[0]>='A' && name[0]<='Z') || name[0]=='_'))throw std::runtime_error("Invalid localization placeholder: "+name);
        for(char k:name)if(!((k>='a' && k<='z') || (k>='A' && k<='Z') || (k>='0' && k<='9') || k=='_'))throw std::runtime_error("Invalid localization placeholder: "+name);
        if(names)names->insert(name);
        if(params){if(!params->contains(name))throw std::runtime_error("Missing localization parameter: "+name);const auto& value=params->at(name);out+=value.is_string()?value.get<std::string>():value.dump();}i=end+1;
    }return out;
}
std::set<std::string> placeholders(const Json& value){std::set<std::string> result;if(value.is_string())format(value.get<std::string>(),nullptr,&result);else for(auto& text:value)format(text.get<std::string>(),nullptr,&result);return result;}
void flatten(const Json& node,const std::string& prefix,std::map<std::string,Json>& result){
    if(node.is_string()){if(prefix.empty() || !result.emplace(prefix,node).second)throw std::runtime_error("Empty or duplicate translation key: "+prefix);placeholders(node);return;}
    if(!node.is_object())throw std::runtime_error("Translation must be string, plural forms or object: "+prefix);
    bool forms=!node.empty();for(auto it=node.begin();it!=node.end();++it)forms=forms && categories.count(it.key());
    if(forms){if(prefix.empty() || !node.contains("other"))throw std::runtime_error("Plural translation needs other: "+prefix);for(auto& text:node)if(!text.is_string())throw std::runtime_error("Plural forms must be strings: "+prefix);if(!result.emplace(prefix,node).second)throw std::runtime_error("Duplicate translation key: "+prefix);placeholders(node);return;}
    for(auto it=node.begin();it!=node.end();++it){if(it.key().empty())throw std::runtime_error("Empty translation key");flatten(it.value(),prefix.empty()?it.key():prefix+"."+it.key(),result);}
}
std::vector<std::string> chain(const Localization& state,const std::string& code){auto result=parents(code);for(auto& root:{state.fallbackLanguage,state.defaultLanguage})for(auto& next:parents(root))if(std::find(result.begin(),result.end(),next)==result.end())result.push_back(next);return result;}
std::string systemLanguage(){
#ifdef _WIN32
    wchar_t value[LOCALE_NAME_MAX_LENGTH]{};if(GetUserDefaultLocaleName(value,LOCALE_NAME_MAX_LENGTH)){std::string result;for(auto p=value;*p;++p)result+=char(*p);return result;}
#elif defined(__APPLE__)
    auto list=CFLocaleCopyPreferredLanguages();std::string result;if(list && CFArrayGetCount(list)>0){char buffer[256]{};auto value=static_cast<CFStringRef>(CFArrayGetValueAtIndex(list,0));if(CFStringGetCString(value,buffer,sizeof(buffer),kCFStringEncodingUTF8))result=buffer;}if(list)CFRelease(list);if(!result.empty())return result;
#endif
    for(auto key:{"LC_ALL","LC_MESSAGES","LANG"})if(const char* value=std::getenv(key)){std::string code=value;auto end=code.find_first_of(".@");if(end!=std::string::npos)code.resize(end);if(code!="C" && code!="POSIX" && !code.empty())return code;}
    return {};
}
}
void Localization::load(const Config& config,const std::string& previous,bool readPreference){
    Localization candidate;candidate.revision=revision+1;
    auto builtin=Json::parse(localization_data::builtins);for(auto it=builtin.begin();it!=builtin.end();++it){auto& catalog=candidate.catalogs[it.key()];catalog.name=it.key()=="ru"?"Русский":"English";catalog.pluralLanguage=it.key();flatten(it.value(),"",catalog.messages);}
    auto options=config.data.value("localization",Json::object());if(!options.is_object())throw std::runtime_error("localization must be an object");
    candidate.defaultLanguage=tag(options.value("default_language","en"));candidate.fallbackLanguage=tag(options.value("fallback_language","en"));candidate.saveSelection=options.value("save_selection",true);candidate.warnMissing=options.value("warn_missing",true);
    candidate.preferencePath=config.resolve(config.data.value("save_directory","saves")+"/preferences/language.json");
    auto languages=options.value("languages",Json::object());if(!languages.is_object())throw std::runtime_error("localization.languages must be an object");std::set<std::string> declared;
    for(auto it=languages.begin();it!=languages.end();++it){auto code=tag(it.key());if(!declared.insert(code).second)throw std::runtime_error("Duplicate language tag: "+code);auto specification=it.value();if(specification.is_string())specification=Json{{"file",specification}};if(!specification.is_object())throw std::runtime_error("Invalid language specification: "+code);
        auto& catalog=candidate.catalogs[code];catalog.name=specification.value("name",catalog.name.empty()?code:catalog.name);catalog.pluralLanguage=pluralLanguage(specification.value("plural_language",code));
        if(specification.contains("file")){auto file=config.asset("locales",specification["file"]);try{std::map<std::string,Json> messages;flatten(readJson(file),"",messages);for(auto& [key,value]:messages)catalog.messages[key]=value;}catch(const std::exception& error){throw std::runtime_error(file.u8string()+": "+error.what());}}
        for(auto& [key,value]:catalog.messages)if(value.is_object() && catalog.pluralLanguage.empty())throw std::runtime_error("No CLDR plural rule for "+code+"; set plural_language (or other): "+key);
    }
    if(!candidate.catalogs.count(candidate.defaultLanguage) || !candidate.catalogs.count(candidate.fallbackLanguage))throw std::runtime_error("Default/fallback localization language is not registered");
    // Every language must keep the same parameter contract for a shared key.
    std::map<std::string,std::pair<std::string,std::set<std::string>>> contracts;
    for(auto& [code,catalog]:candidate.catalogs)for(auto& [key,value]:catalog.messages){auto names=placeholders(value);auto [found,inserted]=contracts.emplace(key,std::make_pair(code,names));if(!inserted && found->second.second!=names)throw std::runtime_error("Localization placeholder mismatch: "+code+" / "+found->second.first+" / "+key);}
    candidate.language=previous.empty()?candidate.defaultLanguage:installed(candidate.catalogs,previous);if(candidate.language.empty())candidate.language=candidate.defaultLanguage;
    if(previous.empty() && options.value("auto_detect",false)){auto system=systemLanguage();if(!system.empty()){auto selected=installed(candidate.catalogs,system);if(!selected.empty())candidate.language=selected;}}
    if(readPreference && candidate.saveSelection && fs::exists(candidate.preferencePath))try{auto saved=readJson(candidate.preferencePath);if(!saved.is_object() || saved.value("schema_version",0)!=1)throw std::runtime_error("Unsupported language preference format");auto selected=installed(candidate.catalogs,saved.at("language").get<std::string>());if(selected.empty())throw std::runtime_error("Saved language is not available");candidate.language=selected;}catch(const std::exception& error){logger.write("WARN",std::string("Ignoring language preference: ")+error.what());}
    if(!previous.empty() && saveSelection && candidate.saveSelection && !pendingPreference.empty()){auto pending=installed(candidate.catalogs,pendingPreference);if(!pending.empty())candidate.pendingPreference=pending;}
    *this=std::move(candidate);
}
void Localization::select(const std::string& input,bool persist){auto selected=installed(catalogs,input);if(selected.empty())throw std::runtime_error("Language is not registered: "+input);if(language!=selected){language=selected;++revision;}if(persist && saveSelection)pendingPreference=language;}
std::string Localization::translate(const std::string& key,const Json& params){
    if(key.empty() || !params.is_object())throw std::runtime_error("Translation needs a nonempty key and object parameters");
    for(auto& code:chain(*this,language)){auto catalog=catalogs.find(code);if(catalog==catalogs.end())continue;auto found=catalog->second.messages.find(key);if(found==catalog->second.messages.end())continue;
        auto primary=parents(language);if(warnMissing && std::find(primary.begin(),primary.end(),code)==primary.end() && warned.emplace(language,key).second)logger.write("WARN","Localization missing ["+language+"]: "+key+"; using "+code);
        auto value=found->second;if(value.is_object()){if(!params.contains("count"))throw std::runtime_error("Plural translation needs count: "+key);auto form=plural(catalog->second.pluralLanguage,params["count"]);value=value.contains(form)?value.at(form):value.at("other");}
        try{return format(value.get<std::string>(),&params);}catch(const std::exception& error){throw std::runtime_error("Localization ["+code+"] "+key+": "+error.what());}
    }
    if(warnMissing && warned.emplace(language,key).second)logger.write("WARN","Localization key not found ["+language+"]: "+key);return key;
}
bool Localization::has(const std::string& key,const std::string& input,bool fallback)const{auto selected=input.empty()?language:installed(catalogs,input);if(selected.empty())return false;auto codes=fallback?chain(*this,selected):std::vector<std::string>{selected};for(auto& code:codes){auto found=catalogs.find(code);if(found!=catalogs.end() && found->second.messages.count(key))return true;}return false;}
Json Localization::languages()const{Json result=Json::array();for(auto& [code,catalog]:catalogs)result.push_back({{"code",code},{"name",catalog.name}});return result;}
void Localization::flush(){
    if(pendingPreference.empty())return;fs::create_directories(preferencePath.parent_path());auto temporary=preferencePath;temporary+=".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try{{std::ofstream out(temporary,std::ios::binary);if(!out)throw std::runtime_error("Cannot write language preference");out<<Json{{"schema_version",1},{"language",pendingPreference}}.dump(2);out.flush();if(!out)throw std::runtime_error("Language preference write failed");}
#ifdef _WIN32
        if(!MoveFileExW(temporary.wstring().c_str(),preferencePath.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace language preference");
#else
        fs::rename(temporary,preferencePath);
#endif
        pendingPreference.clear();
    }catch(...){std::error_code error;fs::remove(temporary,error);throw;}
}
}

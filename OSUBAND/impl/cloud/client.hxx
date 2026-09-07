#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <regex>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"crypt32.lib")
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"shell32.lib")
namespace cloud {
using json=nlohmann::json;
inline std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)throw std::runtime_error("Invalid UTF-8 text");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
inline std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
inline std::filesystem::path storage(){wchar_t path[MAX_PATH]{};if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,path)))throw std::runtime_error("LocalAppData is unavailable");auto p=std::filesystem::path(path)/L"OSUBAND";std::error_code ec;std::filesystem::create_directories(p,ec);if(ec)throw std::runtime_error("Cannot create OSU!BAND folder");return p;}
inline std::filesystem::path executable_dir(){std::wstring buffer(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!n||n>=buffer.size())throw std::runtime_error("Cannot locate application");buffer.resize(n);return std::filesystem::path(buffer).parent_path();}
inline std::string sha256(const void* data,size_t size){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE h=nullptr;DWORD object_len=0,got=0;std::vector<unsigned char> object;unsigned char digest[32]{};
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA-256 is unavailable");
 NTSTATUS status=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_len),sizeof(object_len),&got,0);object.resize(object_len);
 if(status>=0)status=BCryptCreateHash(alg,&h,object.data(),object_len,nullptr,0,0);
 if(status>=0)status=BCryptHashData(h,reinterpret_cast<PUCHAR>(const_cast<void*>(data)),static_cast<ULONG>(size),0);
 if(status>=0)status=BCryptFinishHash(h,digest,32,0);if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);if(status<0)throw std::runtime_error("SHA-256 failed");
 const char* alphabet="0123456789abcdef";std::string out;for(auto b:digest){out+=alphabet[b>>4];out+=alphabet[b&15];}return out;
}
inline std::string sha256(const std::string& s){return sha256(s.data(),s.size());}
inline std::string machine_id(){
 wchar_t guid[256]{};DWORD len=sizeof(guid);const LSTATUS r=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Cryptography",L"MachineGuid",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,guid,&len);
 DWORD serial=0;GetVolumeInformationW(L"C:\\",nullptr,0,&serial,nullptr,nullptr,nullptr,0);
 if(r!=ERROR_SUCCESS||guid[0]==0)throw std::runtime_error("Cannot read the device identifier");
 return sha256("OSU!BAND/device/v1|"+utf8(guid)+"|"+std::to_string(serial));
}
inline std::string computer_name(){wchar_t name[256]{};DWORD n=256;if(GetComputerNameW(name,&n))return utf8(std::wstring(name,n));return "Windows PC";}
struct internet {HINTERNET h=nullptr;explicit internet(HINTERNET v):h(v){}~internet(){if(h)WinHttpCloseHandle(h);}internet(const internet&)=delete;internet& operator=(const internet&)=delete;operator HINTERNET()const{return h;}};
struct parsed_url{std::wstring host,path;INTERNET_PORT port=443;};
inline parsed_url parse_url(const std::string& url){
 auto w=wide(url);URL_COMPONENTS c{};c.dwStructSize=sizeof(c);c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=c.dwUserNameLength=c.dwPasswordLength=static_cast<DWORD>(-1);
 if(!WinHttpCrackUrl(w.c_str(),static_cast<DWORD>(w.size()),0,&c)||c.nScheme!=INTERNET_SCHEME_HTTPS||c.dwUserNameLength||c.dwPasswordLength)throw std::runtime_error("Only authenticated HTTPS connections are supported");
 parsed_url p;p.host.assign(c.lpszHostName,c.dwHostNameLength);p.path.assign(c.lpszUrlPath,c.dwUrlPathLength);if(c.dwExtraInfoLength)p.path.append(c.lpszExtraInfo,c.dwExtraInfoLength);if(p.path.empty())p.path=L"/";p.port=c.nPort;return p;
}
inline bool valid_origin(const std::string& origin){try{const auto p=parse_url(origin);return p.port==443&&p.path==L"/"&&p.host.find(L'.')!=std::wstring::npos&&origin.find('#')==std::string::npos;}catch(...){return false;}}
struct response{DWORD status=0;std::string text,location;};
struct api_error : std::runtime_error {
 DWORD status;
 api_error(DWORD value,const std::string& message):std::runtime_error(message),status(value){}
};
inline response request(const std::string& url,const wchar_t* method,const std::string& body="",const std::wstring& headers=L"",size_t maximum=1024*1024,std::atomic<float>* progress=nullptr,size_t expected=0){
 const auto p=parse_url(url);internet session(WinHttpOpen(L"OSU!BAND/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));if(!session.h)throw std::runtime_error("Cannot initialize network");
 WinHttpSetTimeouts(session,5000,5000,10000,10000);internet connection(WinHttpConnect(session,p.host.c_str(),p.port,0));if(!connection.h)throw std::runtime_error("Cannot connect to the site");
 internet req(WinHttpOpenRequest(connection,method,p.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));if(!req.h)throw std::runtime_error("Cannot create request");
 DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(req,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
 std::wstring hs=L"Accept: application/json\r\n"+headers;if(!body.empty())hs+=L"Content-Type: application/json\r\n";
 if(!WinHttpSendRequest(req,hs.c_str(),static_cast<DWORD>(hs.size()),body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0)||!WinHttpReceiveResponse(req,nullptr))throw std::runtime_error("Connection failed. Check the site address and internet connection.");
 response out;DWORD size=sizeof(out.status);WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&out.status,&size,WINHTTP_NO_HEADER_INDEX);
 if(out.status>=300&&out.status<400){wchar_t loc[8192]{};DWORD n=sizeof(loc);if(WinHttpQueryHeaders(req,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,loc,&n,WINHTTP_NO_HEADER_INDEX))out.location=utf8(loc);return out;}
 char chunk[32768];DWORD count=0;
 while(true){if(!WinHttpReadData(req,chunk,sizeof(chunk),&count))throw std::runtime_error("Download was interrupted");if(!count)break;if(out.text.size()+count>maximum)throw std::runtime_error("Response exceeds the permitted size");out.text.append(chunk,count);if(progress&&expected)*progress=std::min(.97f,static_cast<float>(out.text.size())/expected);}
 return out;
}
inline void save_encrypted(const json& data){auto raw=data.dump();DATA_BLOB in{static_cast<DWORD>(raw.size()),reinterpret_cast<BYTE*>(raw.data())},out{};
 if(!CryptProtectData(&in,L"OSU!BAND device session",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))throw std::runtime_error("Windows could not protect the session");
 auto path=storage()/L"session.dat",temp=storage()/L"session.tmp";
 bool ok=false;{std::ofstream file(temp,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(out.pbData),out.cbData);file.flush();ok=static_cast<bool>(file);}SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);
 if(!ok||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot save the protected session");
}
inline json load_encrypted(){auto path=storage()/L"session.dat";std::error_code ec;const auto n=std::filesystem::file_size(path,ec);if(ec||n>65536)return json::object();
 std::ifstream file(path,std::ios::binary);std::vector<BYTE> bytes((std::istreambuf_iterator<char>(file)),{});if(bytes.empty())return json::object();
 DATA_BLOB in{static_cast<DWORD>(bytes.size()),bytes.data()},out{};if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return json::object();
 auto result=json::parse(out.pbData,out.pbData+out.cbData,nullptr,false);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return result.is_object()?result:json::object();
}
 struct client {
 std::string origin,access_token,hwid;
 static client restore(){client c;c.hwid=machine_id();auto saved=load_encrypted();c.origin=saved.value("origin","https://osuband-control.zeyg228.workers.dev");c.access_token=saved.value("token","");
  #ifdef OSUBAND_DEVELOPMENT
  auto config=executable_dir()/L"osuband.json";std::ifstream f(config);if(f){auto j=json::parse(f,nullptr,false);if(j.is_object()){auto configured=j.value("site","");while(!configured.empty()&&configured.back()=='/')configured.pop_back();if(valid_origin(configured)&&configured!=c.origin){c.origin=configured;c.access_token.clear();}}}
  #else
  if(c.origin!="https://osuband-control.zeyg228.workers.dev")c.access_token.clear();
  c.origin="https://osuband-control.zeyg228.workers.dev";
  #endif
  if(!valid_origin(c.origin)){c.origin.clear();c.access_token.clear();}return c;
 }
 void persist()const{save_encrypted({{"origin",origin},{"token",access_token}});}
 json api(const std::string& path,const json* payload=nullptr,bool authenticated=true,const std::string& runtime_channel="")const{
  if(!valid_origin(origin))throw std::runtime_error("Set your OSU!BAND site address first");
  std::wstring headers=L"X-OSUBAND-HWID: "+wide(hwid)+L"\r\n";
  if(runtime_channel=="stable"||runtime_channel=="lab")headers+=L"X-OSUBAND-CHANNEL: "+wide(runtime_channel)+L"\r\n";
  if(authenticated){if(!std::regex_match(access_token,std::regex("[a-f0-9]{64}")))throw std::runtime_error("Connect your account first");headers+=L"Authorization: Bearer "+wide(access_token)+L"\r\n";}
  auto r=request(origin+"/api/v1/"+path,payload?L"POST":L"GET",payload?payload->dump():"",headers);
  auto data=json::parse(r.text,nullptr,false);if(!data.is_object())throw std::runtime_error("The site did not return an API response. Check its address and hosting settings.");
  if(r.status<200||r.status>=300)throw api_error(r.status,data.value("error","The request failed"));return data;
 }
 json session(const std::string& channel="stable")const{json b={{"hwid",hwid},{"channel",channel}};return api("device/session",&b);}
 json start()const{json b={{"hwid",hwid},{"deviceName",computer_name()}};return api("device/start",&b,false);}
 json poll(const std::string& code)const{json b={{"deviceCode",code}};return api("device/poll",&b,false);}
 void disconnect(){access_token.clear();persist();}
};
inline bool valid_pe(const std::string& bytes){if(bytes.size()<256||bytes[0]!='M'||bytes[1]!='Z')return false;uint32_t off=0;std::memcpy(&off,bytes.data()+0x3c,4);if(off>bytes.size()-26)return false;return bytes.compare(off,4,std::string("PE\0\0",4))==0&&static_cast<unsigned char>(bytes[off+4])==0x64&&static_cast<unsigned char>(bytes[off+5])==0x86;}
inline std::filesystem::path install(const json& manifest,std::atomic<float>& progress){
 const std::string digest=manifest.value("sha256","");const auto expected=manifest.value("size",uint64_t(0));
 if(!std::regex_match(digest,std::regex("[a-f0-9]{64}"))||expected<1024||expected>32*1024*1024)throw std::runtime_error("Invalid release manifest");
 std::string url=manifest.value("url","");const auto initial=parse_url(url);
 if(initial.host!=L"github.com"||url.find("/releases/download/")==std::string::npos||!url.ends_with("/OSUBAND.exe"))throw std::runtime_error("Untrusted release source");
 auto folder=storage()/L"versions"/wide(digest.substr(0,16));std::filesystem::create_directories(folder);auto path=folder/L"OSUBAND.exe";
 if(std::filesystem::exists(path)){std::ifstream f(path,std::ios::binary);std::string data((std::istreambuf_iterator<char>(f)),{});if(data.size()==expected&&sha256(data)==digest&&valid_pe(data)){progress=1;return path;}}
 response r;
 for(int redirects=0;redirects<=5;++redirects){const auto target=parse_url(url);if(target.port!=443||(target.host!=L"github.com"&&target.host!=L"release-assets.githubusercontent.com"&&target.host!=L"objects.githubusercontent.com"))throw std::runtime_error("Release redirected to an untrusted host");
  r=request(url,L"GET","",L"",static_cast<size_t>(expected),&progress,static_cast<size_t>(expected));if(r.status>=300&&r.status<400){if(r.location.empty()||redirects==5)throw std::runtime_error("Too many download redirects");url=r.location;continue;}break;}
 if(r.status!=200||r.text.size()!=expected)throw std::runtime_error("Incomplete release download");if(sha256(r.text)!=digest)throw std::runtime_error("SHA-256 mismatch. The file will not run.");if(!valid_pe(r.text))throw std::runtime_error("The release is not a Windows x64 executable");
 auto temp=folder/L"download.part";{std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.write(r.text.data(),r.text.size());f.flush();if(!f)throw std::runtime_error("Cannot write the downloaded file");}
 if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot install the release");progress=1;return path;
}
inline void launch(const std::filesystem::path& path){SHELLEXECUTEINFOW s{};s.cbSize=sizeof(s);s.fMask=SEE_MASK_NOCLOSEPROCESS;s.lpVerb=L"open";s.lpFile=path.c_str();auto dir=path.parent_path().wstring();s.lpDirectory=dir.c_str();s.nShow=SW_SHOWNORMAL;
 if(!ShellExecuteExW(&s))throw std::runtime_error("Windows could not start OSU!BAND");if(s.hProcess)CloseHandle(s.hProcess);}
}

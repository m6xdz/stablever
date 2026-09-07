#pragma once
#include <impl/cloud/client.hxx>
#include <impl/util/texture_loader.hxx>
#include <future>
#include <unordered_map>
#include <deque>
#include <chrono>
#include <utility>

namespace cloud {
// No device/session headers go to a third-party image host; redirects rejected.
inline std::string avatar_bytes(const std::string& url) {
    if(url.empty())return {};
    try {
        const auto p=parse_url(url);
        if(p.host!=L"cdn.discordapp.com"||p.port!=443||
           !(p.path.starts_with(L"/avatars/")||p.path.starts_with(L"/embed/avatars/")))return {};
        auto r=request(url,L"GET","",L"",1024*1024);
        return r.status==200?std::move(r.text):std::string{};
    } catch(...) { return {}; }
}
class avatar_cache {
    std::unordered_map<std::string,ID3D11ShaderResourceView*> images;
    std::deque<std::string> queue;
    std::future<std::string> job;
    std::string current;
public:
    ~avatar_cache(){clear();}
    void clear(){if(job.valid())job.wait();for(auto& [url,image]:images)if(image)image->Release();images.clear();queue.clear();}
    ID3D11ShaderResourceView* get(const std::string& url) {
        if(url.empty())return nullptr;
        auto it=images.find(url);if(it!=images.end())return it->second;
        if(images.size()<64){images.emplace(url,nullptr);queue.push_back(url);}return nullptr;
    }
    void tick(ID3D11Device* device) {
        if(job.valid()) {
            if(job.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
            images[current]=util::load_texture_from_memory(device,job.get());
        }
        if(!queue.empty()){current=queue.front();queue.pop_front();auto url=current;job=std::async(std::launch::async,[url]{return avatar_bytes(url);});}
    }
};
}
